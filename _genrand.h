#ifndef _LIBUTIL_GENRAND_H_
#define _LIBUTIL_GENRAND_H_

#define S3_RAND_MAX_INT32 0x7fffffff
#include <stdio.h>

/* Win32/WinCE DLL gunk */
//#include <xyzsphinxbase/sphinxbase_export.h>

// /** \file genrand.h
//  *\brief High performance prortable random generator created by Takuji
//  *Nishimura and Makoto Matsumoto.  
//  * 
//  * A high performance which applied Mersene twister primes to generate
//  * random number. If probably seeded, the random generator can achieve 
//  * 19937-bits period.  For technical detail.  Please take a look at 
//  * (FIXME! Need to search for the web site.) http://www.
//  */
// #ifdef __cplusplus
// extern "C" {
// #endif
// #if 0
// /* Fool Emacs. */
// }
// #endif

// /**
//  * Macros to simplify calling of random generator function.
//  *
//  */
// #define s3_rand_seed(s) genrand_seed(s);
// #define s3_rand_int31()  genrand_int31()
// #define s3_rand_real() genrand_real3()
// #define s3_rand_res53()  genrand_res53()

// /**
//  *Initialize the seed of the random generator. 
//  */
// SPHINXBASE_EXPORT
// void XYZ_SB_Genrand::genrand_seed(unsigned long s);

// /**
//  *generates a random number on [0,0x7fffffff]-interval 
//  */
// SPHINXBASE_EXPORT
// long XYZ_SB_Genrand::genrand_int31(void);

// /**
//  *generates a random number on (0,1)-real-interval 
//  */
// SPHINXBASE_EXPORT
// double XYZ_SB_Genrand::genrand_real3(void);

// /**
//  *generates a random number on [0,1) with 53-bit resolution
//  */
// SPHINXBASE_EXPORT
// double XYZ_SB_Genrand::genrand_res53(void);

// #ifdef __cplusplus
// }
// #endif




//#include <stdio.h>

//#include "xyzsphinxbase/genrand.h"

/* Period parameters */
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */


class XYZ_SB_Genrand {

    private:
        unsigned long mt[N];     /* the array for the state vector  */
        int mti = N + 1;         /* mti==N+1 means mt[N] is not initialized */

    public:
        // void init_genrand(unsigned long s);

        // void
        // genrand_seed(unsigned long s)
        // {
        //     init_genrand(s);
        // }




        /* initializes mt[N] with a seed */
        void
        init_genrand(unsigned long s)
        {
            mt[0] = s & 0xffffffffUL;
            for (mti = 1; mti < N; mti++) {
                mt[mti] =
                    (1812433253UL * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + mti);
                /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
                /* In the previous versions, MSBs of the seed affect   */
                /* only MSBs of the array mt[].                        */
                /* 2002/01/09 modified by Makoto Matsumoto             */
                mt[mti] &= 0xffffffffUL;
                /* for >32 bit machines */
            }
        }

        /* generates a random number on [0,0xffffffff]-interval */
        unsigned long
        genrand_int32(void)
        {
            unsigned long y;
            static unsigned long mag01[2] = { 0x0UL, MATRIX_A };
            /* mag01[x] = x * MATRIX_A  for x=0,1 */

            if (mti >= N) {             /* generate N words at one time */
                int kk;

                if (mti == N + 1)       /* if init_genrand() has not been called, */
                    init_genrand(5489UL);       /* a default initial seed is used */

                for (kk = 0; kk < N - M; kk++) {
                    y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
                    mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1UL];
                }
                for (; kk < N - 1; kk++) {
                    y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
                    mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
                }
                y = (mt[N - 1] & UPPER_MASK) | (mt[0] & LOWER_MASK);
                mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1UL];

                mti = 0;
            }

            y = mt[mti++];

            /* Tempering */
            y ^= (y >> 11);
            y ^= (y << 7) & 0x9d2c5680UL;
            y ^= (y << 15) & 0xefc60000UL;
            y ^= (y >> 18);

            return y;
        }

        /* generates a random number on [0,0x7fffffff]-interval */
        long
        genrand_int31(void)
        {
            return (long) (genrand_int32() >> 1);
        }

        /* generates a random number on [0,1]-real-interval */
        double
        genrand_real1(void)
        {
            return genrand_int32() * (1.0 / 4294967295.0);
            /* divided by 2^32-1 */
        }

        /* generates a random number on [0,1)-real-interval */
        double
        genrand_real2(void)
        {
            return genrand_int32() * (1.0 / 4294967296.0);
            /* divided by 2^32 */
        }

        /* generates a random number on (0,1)-real-interval */
        double
        genrand_real3(void)
        {
            return (((double) genrand_int32()) + 0.5) * (1.0 / 4294967296.0);
            /* divided by 2^32 */
        }

        /* generates a random number on [0,1) with 53-bit resolution*/
        double
        genrand_res53(void)
        {
            unsigned long a = genrand_int32() >> 5, b = genrand_int32() >> 6;
            return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
        }

/* These real versions are due to Isaku Wada, 2002/01/09 added */
};

#endif /*_LIBUTIL_GENRAND_H_*/