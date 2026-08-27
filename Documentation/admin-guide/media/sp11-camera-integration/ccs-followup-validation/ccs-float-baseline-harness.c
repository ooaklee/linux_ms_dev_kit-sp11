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
static u32 float_to_u32_mul_1000000(struct i2c_client *client, u32 phloat)
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

int main(int argc, char **argv)
{
    struct i2c_client client = {0};
    if (argc != 2) return 2;
    u32 value = (u32)strtoul(argv[1], NULL, 0);
    u32 result = float_to_u32_mul_1000000(&client, value);
    printf("input=0x%08" PRIx32 " result=%" PRIu32 "\n", value, result);
    return 0;
}
