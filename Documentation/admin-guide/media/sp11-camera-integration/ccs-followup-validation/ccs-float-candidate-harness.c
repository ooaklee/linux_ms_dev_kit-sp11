#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
struct device { int unused; };
struct i2c_client { struct device dev; };
#define dev_err(dev, fmt, ...) ((void)(dev))
static u32 original_convert(struct i2c_client *client, u32 phloat)
{
	s32 exp;
	u64 man;

	if (phloat >= 0x80000000) {
		dev_err(&client->dev, "this is a negative number\n");
		return 0;
	}

	if (phloat == 0x7f800000)
		return ~0; /* Inf. */

	if ((phloat & 0x7f800000) == 0x7f800000) {
		dev_err(&client->dev, "NaN or other special number\n");
		return 0;
	}

	/* Valid cases begin here */
	if (phloat == 0)
		return 0; /* Valid zero */

	if (phloat > 0x4f800000)
		return ~0; /* larger than 4294967295 */

	/*
	 * Unbias exponent (note how phloat is now guaranteed to
	 * have 0 in the high bit)
	 */
	exp = ((int32_t)phloat >> 23) - 127;

	/* Extract mantissa, add missing '1' bit and it's in MHz */
	man = ((phloat & 0x7fffff) | 0x800000) * 1000000ULL;

	if (exp < 0)
		man >>= -exp;
	else
		man <<= exp;

	man >>= 23; /* Remove mantissa bias */

	return man & 0xffffffff;
}
static u32 candidate_convert(struct i2c_client *client, u32 phloat)
{
	s32 exp;
	u64 man;

	if (phloat >= 0x80000000) {
		dev_err(&client->dev, "this is a negative number\n");
		return 0;
	}

	if (phloat == 0x7f800000)
		return ~0; /* Inf. */

	if ((phloat & 0x7f800000) == 0x7f800000) {
		dev_err(&client->dev, "NaN or other special number\n");
		return 0;
	}

	/* Valid cases begin here */
	if (phloat == 0)
		return 0; /* Valid zero */

	if (phloat > 0x4f800000)
		return ~0; /* larger than 4294967295 */

	/*
	 * Unbias exponent (note how phloat is now guaranteed to
	 * have 0 in the high bit)
	 */
	exp = ((int32_t)phloat >> 23) - 127;
	/* Such small values round down to zero after scaling. */
	if (exp <= -64)
		return 0;

	/* Extract mantissa, add missing '1' bit and it's in MHz */
	man = ((phloat & 0x7fffff) | 0x800000) * 1000000ULL;

	if (exp < 0)
		man >>= -exp;
	else
		man <<= exp;

	man >>= 23; /* Remove mantissa bias */

	return man & 0xffffffff;
}

static int legacy_defined(u32 value)
{
    if (value >= UINT32_C(0x80000000) || value == 0 ||
        (value & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) ||
        value > UINT32_C(0x4f800000))
        return 1;
    return ((int32_t)value >> 23) - 127 > -64;
}
static int check_preserved(struct i2c_client *client, u32 value,
                           unsigned long *preserved, unsigned long *underflow)
{
    u32 actual = candidate_convert(client, value);
    if (legacy_defined(value)) {
        u32 old = original_convert(client, value);
        if (actual != old) {
            fprintf(stderr,"legacy mismatch 0x%08" PRIx32 "\n",value);
            return 1;
        }
        (*preserved)++;
    } else {
        if (actual != 0) {
            fprintf(stderr,"underflow mismatch 0x%08" PRIx32 "\n",value);
            return 1;
        }
        (*underflow)++;
    }
    return 0;
}
int main(void)
{
    struct i2c_client client = {0};
    const struct { u32 value; u32 expected; } cases[] = {
        { UINT32_C(0x00000000), UINT32_C(0) },
        { UINT32_C(0x80000000), UINT32_C(0) },
        { UINT32_C(0xbf800000), UINT32_C(0) },
        { UINT32_C(0x7f800000), UINT32_C(4294967295) },
        { UINT32_C(0x7fc00000), UINT32_C(0) },
        { UINT32_C(0x4f800001), UINT32_C(4294967295) },
        { UINT32_C(0x4f800000), UINT32_C(0) },
        { UINT32_C(0x3f800000), UINT32_C(1000000) },
        { UINT32_C(0x3f000000), UINT32_C(500000) },
        { UINT32_C(0x40400000), UINT32_C(3000000) },
        { UINT32_C(0x44834000), UINT32_C(1050000000) },
        { UINT32_C(0x451c4000), UINT32_C(2500000000) },
        { UINT32_C(0x41960000), UINT32_C(18750000) },
        { UINT32_C(0x4332947b), UINT32_C(178580001) },
        { UINT32_C(0x00000001), UINT32_C(0) },
        { UINT32_C(0x007fffff), UINT32_C(0) },
        { UINT32_C(0x00800000), UINT32_C(0) },
        { UINT32_C(0x044f3000), UINT32_C(0) },
        { UINT32_C(0x1f000000), UINT32_C(0) },
        { UINT32_C(0x1f800000), UINT32_C(0) },
        { UINT32_C(0x1fffffff), UINT32_C(0) },
        { UINT32_C(0x20000000), UINT32_C(0) },
        { UINT32_C(0x20000001), UINT32_C(0) },
        { UINT32_C(0x358637bd), UINT32_C(0) },
        { UINT32_C(0x358637be), UINT32_C(1) },
    };
    unsigned long preserved=0, underflow=0;
    for (unsigned int i=0; i<sizeof(cases)/sizeof(cases[0]);i++) {
        u32 actual=candidate_convert(&client,cases[i].value);
        if (actual!=cases[i].expected) {
            fprintf(stderr,"case 0x%08" PRIx32 " expected %" PRIu32 " got %" PRIu32 "\n",cases[i].value,cases[i].expected,actual);
            return 1;
        }
    }
    const u32 mantissas[] = {0,1,UINT32_C(0x7fffff)};
    for (unsigned int sign=0;sign<2;sign++)
        for (unsigned int exponent=0;exponent<256;exponent++)
            for (unsigned int m=0;m<3;m++) {
                u32 value=(sign<<31)|(exponent<<23)|mantissas[m];
                if (check_preserved(&client,value,&preserved,&underflow)) return 1;
            }
    u32 random=UINT32_C(0x681);
    for (unsigned int i=0;i<1000000;i++) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        if (check_preserved(&client,random,&preserved,&underflow)) return 1;
    }
    printf("explicit_cases=%zu exponent_sign_mantissa_cases=1536 deterministic_patterns=1000000 preserved_defined_results=%lu tiny_underflows_zero=%lu\n",sizeof(cases)/sizeof(cases[0]),preserved,underflow);
    return 0;
}
