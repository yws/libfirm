/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief    tarval floating point calculations
 * @date     2003-2013
 * @author   Mathias Heil, Michael Beck, Matthias Braun
 */
#include "fltcalc.h"
#include "strcalc.h"
#include "error.h"

#include <math.h>
#include <inttypes.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

#include "xmalloc.h"

/** The number of extra precision rounding bits */
#define ROUNDING_BITS 2

/* our floating point value */
struct fp_value {
	float_descriptor_t desc;
	unsigned char      clss;
	bool               sign;
	/** exp[value_size] + mant[value_size].
	 * Mantissa has an explicit one at the beginning (contrary to many
	 * floatingpoint formats) */
	sc_word            value[];
};

#define _exp(a)  &((a)->value[0])
#define _mant(a) &((a)->value[value_size])

/** A temporary buffer. */
static fp_value *calc_buffer;

/** Current rounding mode.*/
static fc_rounding_mode_t rounding_mode;

static unsigned calc_buffer_size;
static unsigned value_size;
static unsigned max_precision;

/** Exact flag. */
static bool fc_exact = true;

static float_descriptor_t long_double_desc;

/** pack machine-like */
static void pack(const fp_value *value, sc_word *packed)
{
	switch ((value_class_t)value->clss) {
	case FC_NAN: {
		fp_value *val_buffer = (fp_value*) alloca(calc_buffer_size);
		fc_get_qnan(&value->desc, val_buffer);
		value = val_buffer;
		break;
	}

	case FC_INF: {
		fp_value *val_buffer = (fp_value*) alloca(calc_buffer_size);
		fc_get_inf(&value->desc, val_buffer, value->sign);
		value = val_buffer;
		break;
	}

	default:
		break;
	}

	const float_descriptor_t *desc = &value->desc;
	unsigned mantissa_size = desc->mantissa_size;
	unsigned exponent_size = desc->exponent_size;

	/* extract mantissa, remove rounding bits */
	/* remove rounding bits */
	sc_shrI(_mant(value), ROUNDING_BITS, packed);
	sc_zero_extend(packed, mantissa_size);

	/* pack exponent: move it to the left after mantissa */
	sc_word *temp = ALLOCAN(sc_word, value_size);
	sc_shlI(_exp(value), mantissa_size, temp);
	sc_or(packed, temp, packed);
	sc_zero_extend(packed, mantissa_size + exponent_size);

	/* set sign bit */
	if (value->sign)
		sc_set_bit_at(packed, mantissa_size + exponent_size);
}

/**
 * Normalize a fp_value.
 *
 * @return true if result is exact
 */
static bool normalize(const fp_value *in_val, fp_value *out_val, bool sticky)
{
	const float_descriptor_t *desc = &in_val->desc;
	unsigned effective_mantissa = desc->mantissa_size - desc->explicit_one;
	/* save rounding bits at the end */
	int hsb = ROUNDING_BITS + effective_mantissa - sc_get_highest_set_bit(_mant(in_val)) - 1;

	if (in_val != out_val) {
		out_val->sign = in_val->sign;
		out_val->desc = in_val->desc;
	}

	out_val->clss = FC_NORMAL;

	/* mantissa all zeros, so zero exponent (because of explicit one) */
	if (hsb == ROUNDING_BITS + (int)effective_mantissa) {
		sc_zero(_exp(out_val));
		hsb = -1;
	}

	/* shift the first 1 into the left of the radix point (i.e. hsb == -1) */
	bool  exact = true;
	sc_word *temp  = ALLOCAN(sc_word, value_size);
	if (hsb < -1) {
		/* shift right */
		sc_val_from_ulong(-hsb-1, temp);

		bool carry = sc_shrI(_mant(in_val), -hsb-1, _mant(out_val));

		/* remember if some bits were shifted away */
		if (carry) {
			exact  = false;
			sticky = true;
		}
		sc_add(_exp(in_val), temp, _exp(out_val));
	} else if (hsb > -1) {
		/* shift left */
		sc_val_from_ulong(hsb+1, temp);

		sc_shlI(_mant(in_val), hsb+1, _mant(out_val));

		sc_sub(_exp(in_val), temp, _exp(out_val));
	}

	/* check for exponent underflow */
	if (sc_is_negative(_exp(out_val))
	 || sc_is_zero(_exp(out_val), value_size)) {
		/* exponent underflow */
		/* shift the mantissa right to have a zero exponent */
		sc_val_from_ulong(1, temp);
		sc_sub(temp, _exp(out_val), temp);

		bool carry = sc_shr(_mant(out_val), temp, _mant(out_val));
		if (carry) {
			exact  = false;
			sticky = true;
		}
		/* denormalized means exponent of zero */
		sc_zero(_exp(out_val));

		out_val->clss = FC_SUBNORMAL;
	}

	/* perform rounding by adding a value that clears the guard bit and the
	 * round bit and either causes a carry to round up or not */
	/* get the last 3 bits of the value */
	char lsb       = sc_sub_bits(_mant(out_val), effective_mantissa + ROUNDING_BITS, 0) & 0x7;
	char guard     = (lsb&0x2)>>1;
	char round     = lsb&0x1;
	char round_dir = 0;
	switch (rounding_mode) {
	case FC_TONEAREST:
		/* round to nearest representable value, if in doubt choose the version
		 * with lsb == 0 */
		round_dir = guard && (sticky || round || lsb>>2);
		break;
	case FC_TOPOSITIVE:
		/* if positive: round to one if the exact value is bigger, else to zero */
		round_dir = (!out_val->sign && (guard || round || sticky));
		break;
	case FC_TONEGATIVE:
		/* if negative: round to one if the exact value is bigger, else to zero */
		round_dir = (out_val->sign && (guard || round || sticky));
		break;
	case FC_TOZERO:
		/* always round to 0 (chopping mode) */
		round_dir = 0;
		break;
	}

	if (round_dir == 1) {
		guard = (round^guard)<<1;
		lsb = !(round || guard)<<2 | guard | round;
	} else {
		lsb = -((guard<<1) | round);
	}

	/* add the rounded value */
	if (lsb != 0) {
		sc_val_from_long(lsb, temp);
		sc_add(_mant(out_val), temp, _mant(out_val));
		exact = false;
	}

	/* could have rounded down to zero */
	if (sc_is_zero(_mant(out_val), value_size)
	    && (out_val->clss == FC_SUBNORMAL))
		out_val->clss = FC_ZERO;

	/* check for rounding overflow */
	hsb = ROUNDING_BITS + effective_mantissa - sc_get_highest_set_bit(_mant(out_val)) - 1;
	if ((out_val->clss != FC_SUBNORMAL) && (hsb < -1)) {
		sc_val_from_ulong(1, temp);
		bool carry = sc_shrI(_mant(out_val), 1, _mant(out_val));
		if (carry)
			exact = false;
		sc_add(_exp(out_val), temp, _exp(out_val));
	} else if ((out_val->clss == FC_SUBNORMAL) && (hsb == -1)) {
		/* overflow caused the mantissa to be normal again,
		 * so adapt the exponent accordingly */
		sc_inc(_exp(out_val), _exp(out_val));

		out_val->clss = FC_NORMAL;
	}
	/* no further rounding is needed, because rounding overflow means
	 * the carry of the original rounding was propagated all the way
	 * up to the bit left of the radix point. This implies the bits
	 * to the right are all zeros (rounding is +1) */

	/* check for exponent overflow */
	sc_val_from_ulong((1 << out_val->desc.exponent_size) - 1, temp);
	if (sc_comp(_exp(out_val), temp) != ir_relation_less) {
		/* exponent overflow, reaction depends on rounding method:
		 *
		 * mode        | sign of value |  result
		 *--------------------------------------------------------------
		 * TO_NEAREST  |      +        |   +inf
		 *             |      -        |   -inf
		 *--------------------------------------------------------------
		 * TO_POSITIVE |      +        |   +inf
		 *             |      -        |   smallest representable value
		 *--------------------------------------------------------------
		 * TO_NEGATIVE |      +        |   largest representable value
		 *             |      -        |   -inf
		 *--------------------------------------------------------------
		 * TO_ZERO     |      +        |   largest representable value
		 *             |      -        |   smallest representable value
		 *--------------------------------------------------------------*/
		switch (rounding_mode) {
		case FC_TONEAREST:
		case FC_TOPOSITIVE:
			fc_get_inf(&out_val->desc, out_val, out_val->sign);
			break;

		case FC_TONEGATIVE:
		case FC_TOZERO:
			fc_get_max(&out_val->desc, out_val, out_val->sign);
			break;
		}
	}
	return exact;
}

/**
 * Operations involving NaN's must return NaN.
 * They are NOT exact.
 */
static bool handle_NAN(const fp_value *a, const fp_value *b, fp_value *result)
{
	if (a->clss == FC_NAN) {
		if (a != result)
			memcpy(result, a, calc_buffer_size);
		fc_exact = false;
		return true;
	}
	if (b->clss == FC_NAN) {
		if (b != result) memcpy(result, b, calc_buffer_size);
		fc_exact = false;
		return true;
	}
	return false;
}

/**
 * calculate a + b, where a is the value with the bigger exponent
 */
static void _fadd(const fp_value *a, const fp_value *b, fp_value *result)
{
	fc_exact = true;

	if (handle_NAN(a, b, result))
		return;

	/* make sure result has a descriptor */
	if (result != a && result != b)
		result->desc = a->desc;

	/* determine if this is an addition or subtraction */
	bool sign = a->sign ^ b->sign;

	/* produce NaN on inf - inf */
	if (sign && (a->clss == FC_INF) && (b->clss == FC_INF)) {
		fc_exact = false;
		fc_get_qnan(&a->desc, result);
		return;
	}

	sc_word *temp     = ALLOCAN(sc_word, value_size);
	sc_word *exp_diff = ALLOCAN(sc_word, value_size);

	/* get exponent difference */
	sc_sub(_exp(a), _exp(b), exp_diff);

	/* initially set sign to be the sign of a, special treatment of subtraction
	 * when exponents are equal is required though.
	 * Also special care about the sign is needed when the mantissas are equal
	 * (+/- 0 ?) */
	bool res_sign;
	if (sign && sc_val_to_long(exp_diff) == 0) {
		switch (sc_comp(_mant(a), _mant(b))) {
		case ir_relation_greater:  /* a > b */
			res_sign = a->sign;  /* abs(a) is bigger and a is negative */
			break;
		case ir_relation_equal:  /* a == b */
			res_sign = (rounding_mode == FC_TONEGATIVE);
			break;
		case ir_relation_less: /* a < b */
			res_sign = b->sign; /* abs(b) is bigger and b is negative */
			break;
		default:
			panic("invalid comparison result");
		}
	} else {
		res_sign = a->sign;
	}
	result->sign = res_sign;

	/* sign has been taken care of, check for special cases */
	if (a->clss == FC_ZERO || b->clss == FC_INF) {
		if (b != result)
			memcpy(result, b, calc_buffer_size);
		fc_exact = b->clss == FC_NORMAL;
		result->sign = res_sign;
		return;
	}
	if (b->clss == FC_ZERO || a->clss == FC_INF) {
		if (a != result)
			memcpy(result, a, calc_buffer_size);
		fc_exact = a->clss == FC_NORMAL;
		result->sign = res_sign;
		return;
	}

	/* shift the smaller value to the right to align the radix point */
	/* subnormals have their radix point shifted to the right,
	 * take care of this first */
	if ((b->clss == FC_SUBNORMAL) && (a->clss != FC_SUBNORMAL)) {
		sc_val_from_ulong(1, temp);
		sc_sub(exp_diff, temp, exp_diff);
	}

	bool sticky = sc_shr(_mant(b), exp_diff, temp);
	fc_exact &= !sticky;

	if (sticky && sign) {
		/* if subtracting a little more than the represented value or adding a
		 * little more than the represented value to a negative value this, in
		 * addition to the still set sticky bit, takes account of the
		 * 'little more' */
		sc_inc(temp, temp);
	}

	if (sign) {
		if (sc_comp(_mant(a), temp) == ir_relation_less)
			sc_sub(temp, _mant(a), _mant(result));
		else
			sc_sub(_mant(a), temp, _mant(result));
	} else {
		sc_add(_mant(a), temp, _mant(result));
	}

	/* normalize expects a 'normal' radix point, adding two subnormals
	 * results in a subnormal radix point -> shifting before normalizing */
	if ((a->clss == FC_SUBNORMAL) && (b->clss == FC_SUBNORMAL)) {
		sc_shlI(_mant(result), 1, _mant(result));
	}

	/* resulting exponent is the bigger one */
	memmove(_exp(result), _exp(a), value_size);

	fc_exact &= normalize(result, result, sticky);
}

/**
 * calculate a * b
 */
static void _fmul(const fp_value *a, const fp_value *b, fp_value *result)
{
	fc_exact = true;

	if (handle_NAN(a, b, result))
		return;

	if (result != a && result != b)
		result->desc = a->desc;

	bool res_sign;
	result->sign = res_sign = (a->sign ^ b->sign);

	/* produce NaN on 0 * inf */
	if (a->clss == FC_ZERO) {
		if (b->clss == FC_INF) {
			fc_get_qnan(&a->desc, result);
			fc_exact = false;
		} else {
			if (a != result)
				memcpy(result, a, calc_buffer_size);
			result->sign = res_sign;
		}
		return;
	}
	if (b->clss == FC_ZERO) {
		if (a->clss == FC_INF) {
			fc_get_qnan(&a->desc, result);
			fc_exact = false;
		} else {
			if (b != result)
				memcpy(result, b, calc_buffer_size);
			result->sign = res_sign;
		}
		return;
	}

	if (a->clss == FC_INF) {
		fc_exact = false;
		if (a != result)
			memcpy(result, a, calc_buffer_size);
		result->sign = res_sign;
		return;
	}
	if (b->clss == FC_INF) {
		fc_exact = false;
		if (b != result)
			memcpy(result, b, calc_buffer_size);
		result->sign = res_sign;
		return;
	}

	/* exp = exp(a) + exp(b) - excess */
	sc_add(_exp(a), _exp(b), _exp(result));

	sc_word *temp = ALLOCAN(sc_word, value_size);
	sc_val_from_ulong((1 << (a->desc.exponent_size - 1)) - 1, temp);
	sc_sub(_exp(result), temp, _exp(result));

	/* mixed normal, subnormal values introduce an error of 1, correct it */
	if ((a->clss == FC_SUBNORMAL) ^ (b->clss == FC_SUBNORMAL)) {
		sc_inc(_exp(result), _exp(result));
	}

	sc_mul(_mant(a), _mant(b), _mant(result));

	/* realign result: after a multiplication the digits right of the radix
	 * point are the sum of the factors' digits after the radix point. As all
	 * values are normalized they both have the same amount of these digits,
	 * which has to be restored by proper shifting
	 * because of the rounding bits */
	bool sticky = sc_shrI(_mant(result),
	              (result->desc.mantissa_size - result->desc.explicit_one)
	              +ROUNDING_BITS, _mant(result));
	fc_exact &= !sticky;

	fc_exact &= normalize(result, result, sticky);
}

/**
 * calculate a / b
 */
static void _fdiv(const fp_value *a, const fp_value *b, fp_value *result)
{
	fc_exact = true;

	if (handle_NAN(a, b, result))
		return;

	if (result != a && result != b)
		result->desc = a->desc;

	bool res_sign;
	result->sign = res_sign = (a->sign ^ b->sign);

	/* produce FC_NAN on 0/0 and inf/inf */
	if (a->clss == FC_ZERO) {
		if (b->clss == FC_ZERO) {
			/* 0/0 -> NaN */
			fc_get_qnan(&a->desc, result);
			fc_exact = false;
		} else {
			/* 0/x -> 0 */
			if (a != result)
				memcpy(result, a, calc_buffer_size);
			result->sign = res_sign;
		}
		return;
	}

	if (b->clss == FC_INF) {
		fc_exact = false;
		if (a->clss == FC_INF) {
			/* inf/inf -> NaN */
			fc_get_qnan(&a->desc, result);
		} else {
			/* x/inf -> 0 */
			sc_zero(_exp(result));
			sc_zero(_mant(result));
			result->clss = FC_ZERO;
		}
		return;
	}

	if (a->clss == FC_INF) {
		fc_exact = false;
		/* inf/x -> inf */
		if (a != result)
			memcpy(result, a, calc_buffer_size);
		result->sign = res_sign;
		return;
	}
	if (b->clss == FC_ZERO) {
		fc_exact = false;
		/* division by zero */
		fc_get_inf(&a->desc, result, result->sign);
		return;
	}

	/* exp = exp(a) - exp(b) + excess - 1*/
	sc_word *temp = ALLOCAN(sc_word, value_size);
	sc_sub(_exp(a), _exp(b), _exp(result));
	sc_val_from_ulong((1 << (a->desc.exponent_size - 1)) - 2, temp);
	sc_add(_exp(result), temp, _exp(result));

	/* mixed normal, subnormal values introduce an error of 1, correct it */
	if ((a->clss == FC_SUBNORMAL) ^ (b->clss == FC_SUBNORMAL)) {
		sc_inc(_exp(result), _exp(result));
	}

	/* mant(res) = mant(a) / 1/2mant(b) */
	/* to gain more bits of precision in the result the dividend could be
	 * shifted left, as this operation does not loose bits. This would not
	 * fit into the integer precision, but due to the rounding bits (which
	 * are always zero because the values are all normalized) the divisor
	 * can be shifted right instead to achieve the same result */
	sc_word *dividend = ALLOCAN(sc_word, value_size);
	sc_shlI(_mant(a), (result->desc.mantissa_size - result->desc.explicit_one)
	                  + ROUNDING_BITS, dividend);

	sc_word *divisor = ALLOCAN(sc_word, value_size);
	sc_shrI(_mant(b), 1, divisor);
	bool sticky = sc_div(dividend, divisor, _mant(result));
	fc_exact &= !sticky;

	fc_exact &= normalize(result, result, sticky);
}

/**
 * Truncate the fractional part away.
 *
 * This does not clip to any integer range.
 */
static void _trunc(const fp_value *a, fp_value *result)
{
	/* When exponent == 0 all bits left of the radix point
	 * are the integral part of the value. For 15bit exp_size
	 * this would require a left shift of max. 16383 bits which
	 * is too much.
	 * But it is enough to ensure that no bit right of the radix
	 * point remains set. This restricts the interesting
	 * exponents to the interval [0, mant_size-1].
	 * Outside this interval the truncated value is either 0 or
	 * it does not have fractional parts. */

	/* fixme: can be exact */
	fc_exact = false;

	if (a != result) {
		result->desc = a->desc;
		result->clss = a->clss;
	}

	int exp_bias = (1 << (a->desc.exponent_size - 1)) - 1;
	int exp_val  = sc_val_to_long(_exp(a)) - exp_bias;
	if (exp_val < 0) {
		sc_zero(_exp(result));
		sc_zero(_mant(result));
		result->clss = FC_ZERO;
		return;
	}

	unsigned effective_mantissa = a->desc.mantissa_size - a->desc.explicit_one;
	if (exp_val > (int)effective_mantissa) {
		if (a != result)
			memcpy(result, a, calc_buffer_size);
		return;
	}

	/* set up a proper mask to delete all bits right of the
	 * radix point if the mantissa had been shifted until exp == 0 */
	sc_word *temp = ALLOCAN(sc_word, value_size);
	sc_max_from_bits(1 + exp_val, 0, temp);
	sc_shlI(temp, effective_mantissa - exp_val + ROUNDING_BITS, temp);

	/* and the mask and return the result */
	sc_and(_mant(a), temp, _mant(result));

	if (a != result) {
		memcpy(_exp(result), _exp(a), value_size);
		result->sign = a->sign;
	}
}

const void *fc_get_buffer(void)
{
	return calc_buffer;
}

unsigned fc_get_buffer_length(void)
{
	return calc_buffer_size;
}

void *fc_val_from_str(const char *str, size_t len, void *result)
{
	char *buffer = alloca(len + 1);
	memcpy(buffer, str, len);
	buffer[len] = '\0';
	long double val = strtold(buffer, NULL);
	return fc_val_from_ieee754(val, result);
}

fp_value *fc_val_from_bytes(fp_value *result, const unsigned char *buffer,
                            const float_descriptor_t *desc)
{
	if (result == NULL)
		result = calc_buffer;

	/* CLEAR the buffer, else some bits might be uninitialized */
	memset(result, 0, calc_buffer_size);

	unsigned exponent_size = desc->exponent_size;
	unsigned mantissa_size = desc->mantissa_size;
	unsigned sign_bit      = exponent_size + mantissa_size;
	result->desc = *desc;
	sc_val_from_bits(buffer, 0, mantissa_size, _mant(result));
	sc_val_from_bits(buffer, mantissa_size, mantissa_size+exponent_size,
	                 _exp(result));
	result->sign = (buffer[sign_bit/8] & (1u << (sign_bit%8))) != 0;

	/* adjust for rounding bits */
	sc_shlI(_mant(result), ROUNDING_BITS, _mant(result));

	/* check for special values */
	if (sc_is_zero(_exp(result), value_size)) {
		if (sc_is_zero(_mant(result), value_size)) {
			result->clss = FC_ZERO;
		} else {
			result->clss = FC_SUBNORMAL;
			/* normalize expects the radix point to be normal, so shift
			 * mantissa of subnormal origin one to the left */
			sc_shlI(_mant(result), 1, _mant(result));
			normalize(result, result, 0);
		}
	} else if (sc_is_all_one(_exp(result), exponent_size)) {
		unsigned size = mantissa_size + ROUNDING_BITS - desc->explicit_one;
		if (sc_is_zero(_mant(result), size)) {
			if (!desc->explicit_one)
				sc_set_bit_at(_mant(result), ROUNDING_BITS+mantissa_size);
			result->clss = FC_INF;
		} else {
			result->clss = FC_NAN;
		}
	} else {
		result->clss = FC_NORMAL;
		/* we always have an explicit one */
		if (!desc->explicit_one)
			sc_set_bit_at(_mant(result), ROUNDING_BITS+mantissa_size);
		normalize(result, result, 0);
	}
	return result;
}

fp_value *fc_val_from_ieee754(long double l, fp_value *result)
{
	unsigned real_size
		= (long_double_desc.mantissa_size+long_double_desc.exponent_size+1)/8;
	unsigned char *buf = ALLOCAN(unsigned char, real_size);
#ifdef WORDS_BIGENDIAN
	unsigned char *from = (unsigned char*)&l;
	for (unsigned i = 0; i < real_size; ++i)
		buf[i] = from[real_size-1-i];
#else
	memcpy(buf, &l, real_size);
#endif
	return fc_val_from_bytes(result, buf, &long_double_desc);
}

long double fc_val_to_ieee754(const fp_value *val)
{

	fp_value *temp  = (fp_value*) alloca(calc_buffer_size);
	fp_value *value = fc_cast(val, &long_double_desc, temp);

	sc_word *packed = ALLOCAN(sc_word, value_size);
	pack(value, packed);

	char buf[sizeof(long double)];
	unsigned real_size
		= (long_double_desc.mantissa_size+long_double_desc.exponent_size+1)/8;
	for (unsigned i = 0; i < real_size; ++i) {
#ifdef WORDS_BIGENDIAN
		unsigned offset = real_size - i;
#else
		unsigned offset = i;
#endif
		buf[i] = sc_sub_bits(packed, value_size*4, offset);
	}
	memset(buf+real_size, 0, sizeof(long double)-real_size);
	long double result;
	memcpy(&result, buf, sizeof(result));
	return result;
}

static bool is_quiet_nan(const fp_value *value)
{
	assert(value->clss == FC_NAN);
	const float_descriptor_t *desc = &value->desc;
	if (!desc->explicit_one) {
		return sc_get_bit_at(_mant(value), desc->mantissa_size-1);
	} else {
		return !sc_get_bit_at(_mant(value), desc->mantissa_size-2);
	}
}

fp_value *fc_cast(const fp_value *value, const float_descriptor_t *dest,
                  fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;
	assert(value != result);

	const float_descriptor_t *desc = &value->desc;
	if (desc->exponent_size == dest->exponent_size &&
		desc->mantissa_size == dest->mantissa_size &&
		desc->explicit_one  == dest->explicit_one) {
		if (value != result)
			memcpy(result, value, calc_buffer_size);
		return result;
	}

	if (value->clss == FC_NAN) {
		/* TODO: preserve mantissa bits? */
		return is_quiet_nan(value) ? fc_get_qnan(dest, result)
		                           : fc_get_snan(dest, result);
	} else if (value->clss == FC_INF) {
		return fc_get_inf(dest, result, value->sign);
	}

	/* set the descriptor of the new value */
	result->desc = *dest;
	result->clss = value->clss;
	result->sign = value->sign;

	/* when the mantissa sizes differ normalizing has to shift to align it.
	 * this would change the exponent, which is unwanted. So calculate this
	 * offset and add it */
	int val_bias = (1 << (desc->exponent_size - 1)) - 1;
	int res_bias = (1 << (dest->exponent_size - 1)) - 1;

	int exp_offset = res_bias - val_bias;
	exp_offset    += dest->mantissa_size - dest->explicit_one
	               - (desc->mantissa_size - desc->explicit_one);
	sc_word *temp = ALLOCAN(sc_word, value_size);
	sc_val_from_long(exp_offset, temp);
	sc_add(_exp(value), temp, _exp(result));

	/* normalize expects normalized radix point */
	if (value->clss == FC_SUBNORMAL) {
		sc_shlI(_mant(value), 1, _mant(result));
	} else if (value != result) {
		memcpy(_mant(result), _mant(value), value_size);
	}

	normalize(result, result, 0);
	return result;
}

fp_value *fc_get_max(const float_descriptor_t *desc, fp_value *result, bool sign)
{
	if (result == NULL)
		result = calc_buffer;

	result->desc = *desc;
	result->clss = FC_NORMAL;
	result->sign = sign;

	sc_val_from_ulong((1 << desc->exponent_size) - 2, _exp(result));
	sc_max_from_bits(desc->mantissa_size+1-desc->explicit_one, 0, _mant(result));
	sc_shlI(_mant(result), ROUNDING_BITS, _mant(result));
	return result;
}

fp_value *fc_get_small(const float_descriptor_t *desc, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	result->desc = *desc;
	result->clss = FC_NORMAL;
	result->sign = false;
	sc_val_from_ulong(1, _exp(result));
	sc_zero(_mant(result));
	sc_set_bit_at(_mant(result), (desc->mantissa_size - desc->explicit_one)
	                             + ROUNDING_BITS);
	return result;
}

fp_value *fc_get_epsilon(const float_descriptor_t *desc, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	result->desc = *desc;
	result->clss = FC_NORMAL;
	result->sign = false;

	int      exp_bias           = (1 << (desc->exponent_size-1)) -1;
	unsigned effective_mantissa = desc->mantissa_size - desc->explicit_one;
	sc_val_from_ulong(exp_bias - effective_mantissa, _exp(result));
	sc_zero(_mant(result));
	sc_set_bit_at(_mant(result), effective_mantissa + ROUNDING_BITS);
	return result;
}

fp_value *fc_get_snan(const float_descriptor_t *desc, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	result->desc = *desc;
	result->clss = FC_NAN;
	result->sign = 0;

	sc_max_from_bits(desc->exponent_size, 0, _exp(result));

	/* signaling NaN has msb in mantissa cleared */
	sc_zero(_mant(result));
	/* we still set our explicit one */
	if (desc->explicit_one) {
		sc_set_bit_at(_mant(result), desc->mantissa_size+ROUNDING_BITS-1);
		sc_set_bit_at(_mant(result), desc->mantissa_size+ROUNDING_BITS-3);
	}
	return result;
}

fp_value *fc_get_qnan(const float_descriptor_t *desc, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	result->desc = *desc;
	result->clss = FC_NAN;
	result->sign = 0;

	sc_max_from_bits(desc->exponent_size, 0, _exp(result));

	/* quiet NaN has the msb of the mantissa set, so shift one there */
	sc_zero(_mant(result));
	sc_set_bit_at(_mant(result), desc->mantissa_size+ROUNDING_BITS-1);
	if (desc->explicit_one)
		sc_set_bit_at(_mant(result), desc->mantissa_size+ROUNDING_BITS-2);
	return result;
}

fp_value *fc_get_inf(const float_descriptor_t *desc, fp_value *result,
                     bool sign)
{
	if (result == NULL)
		result = calc_buffer;

	result->desc = *desc;
	result->clss = FC_INF;
	result->sign = sign;
	sc_max_from_bits(desc->exponent_size, 0, _exp(result));
	sc_zero(_mant(result));
	// set the explicit one
	sc_set_bit_at(_mant(result),
				  (desc->mantissa_size - desc->explicit_one)+ROUNDING_BITS);
	return result;
}

ir_relation fc_comp(fp_value const *const val_a, fp_value const *const val_b)
{
	/* shortcut: if both values are identical, they are either
	 * Unordered if NaN or equal
	 */
	if (val_a == val_b)
		return val_a->clss == FC_NAN ? ir_relation_unordered : ir_relation_equal;

	/* unordered if one is a NaN */
	if (val_a->clss == FC_NAN || val_b->clss == FC_NAN)
		return ir_relation_unordered;

	/* zero is equal independent of sign */
	if ((val_a->clss == FC_ZERO) && (val_b->clss == FC_ZERO))
		return ir_relation_equal;

	/* different signs make compare easy */
	if (val_a->sign != val_b->sign)
		return val_a->sign == 0 ? ir_relation_greater : ir_relation_less;

	ir_relation const mul = val_a->sign ? ir_relation_less_greater : ir_relation_false;

	/* both infinity means equality */
	if ((val_a->clss == FC_INF) && (val_b->clss == FC_INF))
		return ir_relation_equal;

	/* infinity is bigger than the rest */
	if (val_a->clss == FC_INF)
		return ir_relation_greater ^ mul;
	if (val_b->clss == FC_INF)
		return ir_relation_less ^ mul;

	/* check first exponent, that mantissa if equal */
	ir_relation rel = sc_comp(_exp(val_a), _exp(val_b));
	if (rel == ir_relation_equal)
		rel = sc_comp(_mant(val_a), _mant(val_b));
	if (rel != ir_relation_equal)
		rel ^= mul;
	return rel;
}

bool fc_is_zero(const fp_value *a)
{
	return a->clss == FC_ZERO;
}

bool fc_is_negative(const fp_value *a)
{
	return a->sign;
}

bool fc_is_inf(const fp_value *a)
{
	return a->clss == FC_INF;
}

bool fc_is_nan(const fp_value *a)
{
	return a->clss == FC_NAN;
}

bool fc_is_subnormal(const fp_value *a)
{
	return a->clss == FC_SUBNORMAL;
}

int fc_print(const fp_value *val, char *buf, size_t buflen, fc_base_t base)
{
	switch (base) {
	case FC_DEC:
		switch ((value_class_t)val->clss) {
		case FC_INF:
			return snprintf(buf, buflen, "%cINF", val->sign ? '-' : '+');
		case FC_NAN:
			return snprintf(buf, buflen, "NaN");
		case FC_ZERO:
			return snprintf(buf, buflen, "0.0");
		default: {
			long double flt_val = fc_val_to_ieee754(val);
			/* XXX 30 is arbitrary */
			return snprintf(buf, buflen, "%.30LE", flt_val);
		}
		}

	case FC_HEX:
		switch ((value_class_t)val->clss) {
		case FC_INF:
			return snprintf(buf, buflen, "%cINF", val->sign ? '-' : '+');
		case FC_NAN:
			return snprintf(buf, buflen, "NaN");
		case FC_ZERO:
			return snprintf(buf, buflen, "0.0");
		default: {
			long double flt_val = fc_val_to_ieee754(val);
			return snprintf(buf, buflen, "%LA", flt_val);
		}
		}

	case FC_PACKED:
	default: {
		sc_word *packed = ALLOCAN(sc_word, value_size);
		pack(val, packed);
		return snprintf(buf, buflen, "%s", sc_print(packed, value_size*4, SC_HEX, 0));
	}
	}
}

unsigned char fc_sub_bits(const fp_value *value, unsigned num_bits,
                          unsigned byte_ofs)
{
	/* this is used to cache the packed version of the value */
	sc_word *packed_value = ALLOCAN(sc_word, value_size);
	pack(value, packed_value);
	return sc_sub_bits(packed_value, num_bits, byte_ofs);
}

void fc_val_to_bytes(const fp_value *value, unsigned char *buffer)
{
	sc_word *packed_value = ALLOCAN(sc_word, value_size);
	pack(value, packed_value);
	const float_descriptor_t *desc = &value->desc;
	unsigned n_bits = desc->mantissa_size + desc->exponent_size + 1;
	assert(n_bits % 8 == 0);
	unsigned n_bytes = n_bits / 8;
	sc_val_to_bytes(packed_value, buffer, n_bytes);
}

bool fc_zero_mantissa(const fp_value *value)
{
	const float_descriptor_t *desc = &value->desc;
	return sc_is_zero(_mant(value),
	                  desc->mantissa_size+ ROUNDING_BITS-desc->explicit_one);
}

int fc_get_exponent(const fp_value *value)
{
	int exp_bias = (1 << (value->desc.exponent_size - 1)) - 1;
	return sc_val_to_long(_exp(value)) - exp_bias;
}

bool fc_can_lossless_conv_to(const fp_value *value,
                             const float_descriptor_t *desc)
{
	/* handle some special cases first */
	switch (value->clss) {
	case FC_ZERO:
	case FC_INF:
	case FC_NAN:
		return true;
	default:
		break;
	}

	/* check if the exponent can be encoded: note, 0 and all ones are reserved
	 * for the exponent */
	int exp_bias = (1 << (desc->exponent_size - 1)) - 1;
	int v        = fc_get_exponent(value) + exp_bias;
	if (0 < v && v < (1 << desc->exponent_size) - 1) {
		/* exponent can be encoded, now check the mantissa */
		v = (value->desc.mantissa_size - value->desc.explicit_one)
		    + ROUNDING_BITS - sc_get_lowest_set_bit(_mant(value));
		return v <= desc->mantissa_size - desc->explicit_one;
	}
	return false;
}


fc_rounding_mode_t fc_set_rounding_mode(fc_rounding_mode_t mode)
{
	if (mode == FC_TONEAREST || mode == FC_TOPOSITIVE || mode == FC_TONEGATIVE
	 || mode == FC_TOZERO)
		rounding_mode = mode;

	return rounding_mode;
}

fc_rounding_mode_t fc_get_rounding_mode(void)
{
	return rounding_mode;
}

void init_fltcalc(unsigned precision)
{
	if (calc_buffer == NULL) {
		init_strcalc(precision + 2 + ROUNDING_BITS);

		/* needs additionally rounding bits, one bit as explicit 1., and one for
		 * addition overflow */
		max_precision = sc_get_precision() - (2 + ROUNDING_BITS);
		if (max_precision < precision)
			printf("WARNING: not enough precision available, using %u\n", max_precision);

		rounding_mode    = FC_TONEAREST;
		value_size       = sc_get_value_length();
		calc_buffer_size = sizeof(fp_value) + 2*value_size;

		calc_buffer = (fp_value*) xmalloc(calc_buffer_size);
		memset(calc_buffer, 0, calc_buffer_size);

#if LDBL_MANT_DIG == 64
		assert(sizeof(long double) == 12 || sizeof(long double) == 16);
		long_double_desc = (float_descriptor_t) { 15, 64, 1 };
#elif LDBL_MANT_DIG == 53
		assert(sizeof(long double) == 8);
		long_double_desc = (float_descriptor_t) { 11, 52, 0 };
#else
	#error "Unsupported long double format"
#endif
	}
}

void finish_fltcalc(void)
{
	free(calc_buffer);
	calc_buffer = NULL;
}

/* definition of interface functions */
fp_value *fc_add(const fp_value *a, const fp_value *b, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	/* make the value with the bigger exponent the first one */
	if (sc_comp(_exp(a), _exp(b)) == ir_relation_less)
		_fadd(b, a, result);
	else
		_fadd(a, b, result);

	return result;
}

fp_value *fc_sub(const fp_value *a, const fp_value *b, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	fp_value *temp = (fp_value*) alloca(calc_buffer_size);
	memcpy(temp, b, calc_buffer_size);
	temp->sign = !b->sign;
	if (sc_comp(_exp(a), _exp(temp)) == ir_relation_less)
		_fadd(temp, a, result);
	else
		_fadd(a, temp, result);

	return result;
}

fp_value *fc_mul(const fp_value *a, const fp_value *b, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	_fmul(a, b, result);
	return result;
}

fp_value *fc_div(const fp_value *a, const fp_value *b, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	_fdiv(a, b, result);
	return result;
}

fp_value *fc_neg(const fp_value *a, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	if (a != result)
		memcpy(result, a, calc_buffer_size);
	result->sign = !a->sign;
	return result;
}

fp_value *fc_int(const fp_value *a, fp_value *result)
{
	if (result == NULL)
		result = calc_buffer;

	_trunc(a, result);
	return result;
}

flt2int_result_t fc_flt2int(const fp_value *a, sc_word *result,
                            unsigned result_bits, bool result_signed)
{
	switch (a->clss) {
	case FC_ZERO:
		sc_zero(result);
		return FLT2INT_OK;
	case FC_INF:
		return a->sign ? FLT2INT_NEGATIVE_OVERFLOW
					   : FLT2INT_POSITIVE_OVERFLOW;
	case FC_NORMAL:
		if (a->sign && !result_signed) {
			/* FIXME: for now we cannot convert this */
			return FLT2INT_BAD;
		}

		unsigned tgt_bits = result_bits - result_signed;

		int exp_bias = (1 << (a->desc.exponent_size - 1)) - 1;
		int exp_val  = sc_val_to_long(_exp(a)) - exp_bias;
		assert(exp_val >= 0 && "floating point value not integral before fc_flt2int() call");
		/* highest bit outside dst_mode range */
		if (exp_val > (int)tgt_bits || (exp_val == (int)tgt_bits &&
			/* MIN_INT is the only exception allowed */
			(!result_signed || !a->sign ||
              sc_get_highest_set_bit(_mant(a))
              != sc_get_lowest_set_bit(_mant(a))))) {
			return a->sign ? FLT2INT_NEGATIVE_OVERFLOW
						   : FLT2INT_POSITIVE_OVERFLOW;
		}
		unsigned mantissa_size = a->desc.mantissa_size + ROUNDING_BITS;
		int      shift         = exp_val - (mantissa_size-a->desc.explicit_one);

		if (tgt_bits < mantissa_size + 1)
			tgt_bits = mantissa_size + 1;
		else
			tgt_bits += result_signed;

		if (shift > 0) {
			sc_shlI(_mant(a), shift, result);
			sc_zero_extend(result, tgt_bits);
		} else {
			sc_shrI(_mant(a), -shift, result);
		}
		if (a->sign)
			sc_neg(result, result);

		return FLT2INT_OK;
	case FC_NAN:
		break;
	}
	return FLT2INT_BAD;
}

bool fc_is_exact(void)
{
	return fc_exact;
}

#ifdef DEBUG_libfirm
/* helper to print fp_values in a debugger */
void fc_debug(fp_value *value);
void __attribute__((used)) fc_debug(fp_value *value)
{
	printf("Class: %d\n", value->clss);
	printf("Sign: %d\n", value->sign);
	printf("Exponent: %s\n",
	       sc_print(_exp(value), sc_get_precision(), SC_HEX, false));
	printf("Unbiased Exponent: %d\n", fc_get_exponent(value));
	printf("Mantissa: %s\n",
	       sc_print(_mant(value), sc_get_precision(), SC_HEX, false));
	printf("Mantissa w/o round: ");
	sc_word *temp = ALLOCAN(sc_word, value_size);
	sc_shrI(_mant(value), ROUNDING_BITS, temp);
	printf("%s\n", sc_print(temp, sc_get_precision(), SC_HEX, false));
	printf("Mantissa w/o round implicit one: ");
	sc_clear_bit_at(temp, value->desc.mantissa_size);
	printf("%s\n", sc_print(temp, sc_get_precision(), SC_HEX, false));

	char buf[128];
	//fc_print(value, buf, sizeof(buf), FC_DEC);
	//printf("%s\n", buf);
	fc_print(value, buf, sizeof(buf), FC_PACKED);
	printf("Packed: %s\n", buf);
}
#endif
