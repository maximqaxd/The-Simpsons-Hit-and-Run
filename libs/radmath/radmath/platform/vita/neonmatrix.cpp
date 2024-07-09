//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <radmath/radmath.hpp>
#include <assert.h>
extern "C"
{
#include <math_neon.h>
}

//#define P3D_NOASM

namespace RadicalMathLibrary
{

void Matrix::Mult(const Matrix &a, const Matrix &b)
{
    assert(a.m[0][3] == 0.0f);
    assert(a.m[1][3] == 0.0f);
    assert(a.m[2][3] == 0.0f);
    assert(a.m[3][3] == 1.0f);
    assert(b.m[0][3] == 0.0f);
    assert(b.m[1][3] == 0.0f);
    assert(b.m[2][3] == 0.0f);
    assert(b.m[3][3] == 1.0f);

#ifdef P3D_NOASM
    // Mult
    m[0][0] = (a.m[0][0] * b.m[0][0]) + (a.m[0][1] * b.m[1][0]) + (a.m[0][2] * b.m[2][0]);
    m[0][1] = (a.m[0][0] * b.m[0][1]) + (a.m[0][1] * b.m[1][1]) + (a.m[0][2] * b.m[2][1]);
    m[0][2] = (a.m[0][0] * b.m[0][2]) + (a.m[0][1] * b.m[1][2]) + (a.m[0][2] * b.m[2][2]);
    m[0][3] = 0.0f;

    m[1][0] = (a.m[1][0] * b.m[0][0]) + (a.m[1][1] * b.m[1][0]) + (a.m[1][2] * b.m[2][0]);
    m[1][1] = (a.m[1][0] * b.m[0][1]) + (a.m[1][1] * b.m[1][1]) + (a.m[1][2] * b.m[2][1]);
    m[1][2] = (a.m[1][0] * b.m[0][2]) + (a.m[1][1] * b.m[1][2]) + (a.m[1][2] * b.m[2][2]);
    m[1][3] = 0.0f;

    m[2][0] = (a.m[2][0] * b.m[0][0]) + (a.m[2][1] * b.m[1][0]) + (a.m[2][2] * b.m[2][0]);
    m[2][1] = (a.m[2][0] * b.m[0][1]) + (a.m[2][1] * b.m[1][1]) + (a.m[2][2] * b.m[2][1]);
    m[2][2] = (a.m[2][0] * b.m[0][2]) + (a.m[2][1] * b.m[1][2]) + (a.m[2][2] * b.m[2][2]);
    m[2][3] = 0.0f;

    m[3][0] = (a.m[3][0] * b.m[0][0]) + (a.m[3][1] * b.m[1][0]) + (a.m[3][2] * b.m[2][0]) + b.m[3][0];
    m[3][1] = (a.m[3][0] * b.m[0][1]) + (a.m[3][1] * b.m[1][1]) + (a.m[3][2] * b.m[2][1]) + b.m[3][1];
    m[3][2] = (a.m[3][0] * b.m[0][2]) + (a.m[3][1] * b.m[1][2]) + (a.m[3][2] * b.m[2][2]) + b.m[3][2];
    m[3][3] = 1.0f;

#else
    float* m0 = const_cast<float*>(b.m[0]);
    float* m1 = const_cast<float*>(a.m[0]);
    float* d = m[0];
    asm volatile (
	"vld1.32 		{d0, d1}, [%1]!			\n\t"	//q0 = m1
	"vld1.32 		{d2, d3}, [%1]!			\n\t"	//q1 = m1+4
	"vld1.32 		{d4, d5}, [%1]!			\n\t"	//q2 = m1+8
	"vld1.32 		{d6, d7}, [%1]			\n\t"	//q3 = m1+12
	"vld1.32 		{d16, d17}, [%0]!		\n\t"	//q8 = m0
	"vld1.32 		{d18, d19}, [%0]!		\n\t"	//q9 = m0+4
	"vld1.32 		{d20, d21}, [%0]!		\n\t"	//q10 = m0+8
	"vld1.32 		{d22, d23}, [%0]		\n\t"	//q11 = m0+12

	"vmul.f32 		q12, q8, d0[0] 			\n\t"	//q12 = q8 * d0[0]
	"vmul.f32 		q13, q8, d2[0] 			\n\t"	//q13 = q8 * d2[0]
	"vmul.f32 		q14, q8, d4[0] 			\n\t"	//q14 = q8 * d4[0]
	"vmul.f32 		q15, q8, d6[0]	 		\n\t"	//q15 = q8 * d6[0]
	"vmla.f32 		q12, q9, d0[1] 			\n\t"	//q12 = q9 * d0[1]
	"vmla.f32 		q13, q9, d2[1] 			\n\t"	//q13 = q9 * d2[1]
	"vmla.f32 		q14, q9, d4[1] 			\n\t"	//q14 = q9 * d4[1]
	"vmla.f32 		q15, q9, d6[1] 			\n\t"	//q15 = q9 * d6[1]
	"vmla.f32 		q12, q10, d1[0] 		\n\t"	//q12 = q10 * d1[0]
	"vmla.f32 		q13, q10, d3[0] 		\n\t"	//q13 = q10 * d3[0]
	"vmla.f32 		q14, q10, d5[0] 		\n\t"	//q14 = q10 * d5[0]
	"vmla.f32 		q15, q10, d7[0] 		\n\t"	//q15 = q10 * d6[0]
    "vadd.f32 		q15, q15, q11    		\n\t"	//q15 = q15 + q11

	"vst1.32 		{d24, d25}, [%2]! 		\n\t"	//d = q12	
	"vst1.32 		{d26, d27}, [%2]!		\n\t"	//d+4 = q13	
	"vst1.32 		{d28, d29}, [%2]! 		\n\t"	//d+8 = q14	
	"vst1.32 		{d30, d31}, [%2]	 	\n\t"	//d+12 = q15	

	: "+r"(m0), "+r"(m1), "+r"(d) :
    : "q0", "q1", "q2", "q3", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15",
	"memory"
	);
#endif
}

void Matrix::MultFull(const Matrix &a, const Matrix &b)
{
    float* m0 = const_cast<float*>(b.m[0]);
    float* m1 = const_cast<float*>(a.m[0]);
#ifdef P3D_NOASM
    matmul4_c(m0, m1, m);
#else
    matmul4_neon(m0, m1, m[0]);
#endif
}

} // End namespace
