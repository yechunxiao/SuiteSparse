//------------------------------------------------------------------------------
// GB_add: C = A+B or C<M>=A+B, but not C<!M>=A+B
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2020, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// GB_add computes C=A+B or C<M>=A+B, using the given operator element-wise on
// the matrices A and B.  The result is typecasted as needed.  The pattern of C
// is the union of the pattern of A and B, intersection with the mask M, if
// present.  The C<!M>=A+B case is not handled; the complemented mask is
// handled in GB_mask.

// Let the op be z=f(x,y) where x, y, and z have type xtype, ytype, and ztype.
// If both A(i,j) and B(i,j) are present, then:

//      C(i,j) = (ctype) op ((xtype) A(i,j), (ytype) B(i,j))

// If just A(i,j) is present but not B(i,j), then:

//      C(i,j) = (ctype) A (i,j)

// If just B(i,j) is present but not A(i,j), then:

//      C(i,j) = (ctype) B (i,j)

// ctype is the type of matrix C.  The pattern of C is the union of A and B.

// op may be NULL.  In this case, the intersection of A and B must be empty.
// This is used by GB_Matrix_wait only, for merging the pending tuple matrix T
// into A.  Any duplicate pending tuples have already been summed in T, so the
// intersection of T and A is always empty.

#include "GB_add.h"

#define GB_FREE_ALL ;

GB_PUBLIC   // accessed by the MATLAB tests in GraphBLAS/Test only
GrB_Info GB_add             // C=A+B or C<M>=A+B
(
    GrB_Matrix *Chandle,    // output matrix (unallocated on input)
    const GrB_Type ctype,   // type of output matrix C
    const bool C_is_csc,    // format of output matrix C
    const GrB_Matrix M,     // optional mask for C, unused if NULL
    const bool Mask_struct, // if true, use the only structure of M
    const GrB_Matrix A,     // input A matrix
    const GrB_Matrix B,     // input B matrix
    const GrB_BinaryOp op,  // op to perform C = op (A,B)
    GB_Context Context
)
{

    //--------------------------------------------------------------------------
    // check inputs
    //--------------------------------------------------------------------------

    GrB_Info info ;
    GBBURBLE ((M == NULL) ? "add " : "masked_add ") ;

    ASSERT (Chandle != NULL) ;
    ASSERT_MATRIX_OK (A, "A for add", GB0) ;
    ASSERT_MATRIX_OK (B, "B for add", GB0) ;
    ASSERT_BINARYOP_OK_OR_NULL (op, "op for add", GB0) ;
    ASSERT_MATRIX_OK_OR_NULL (M, "M for add", GB0) ;
    ASSERT (A->vdim == B->vdim && A->vlen == B->vlen) ;
    if (M != NULL)
    { 
        ASSERT (A->vdim == M->vdim && A->vlen == M->vlen) ;
    }

    // delete any lingering zombies and assemble any pending tuples
    GB_MATRIX_WAIT (M) ;
    GB_MATRIX_WAIT (A) ;
    GB_MATRIX_WAIT (B) ;

    //--------------------------------------------------------------------------
    // initializations
    //--------------------------------------------------------------------------

    GrB_Matrix C = NULL ;
    int64_t Cnvec, Cnvec_nonempty ;
    int64_t *Cp = NULL, *Ch = NULL ;
    int64_t *C_to_M = NULL, *C_to_A = NULL, *C_to_B = NULL ;
    bool Ch_is_Mh ;
    int ntasks, max_ntasks, nthreads ;
    GB_task_struct *TaskList = NULL ;

    //--------------------------------------------------------------------------
    // phase0: determine the vectors in C(:,j)
    //--------------------------------------------------------------------------

    info = GB_add_phase0 (
        // computed by by phase0:
        &Cnvec, &Ch, &C_to_M, &C_to_A, &C_to_B, &Ch_is_Mh,
        // original input:
        M, A, B, Context) ;

    if (info != GrB_SUCCESS)
    { 
        // out of memory
        return (info) ;
    }

    //--------------------------------------------------------------------------
    // phase0b: split C into tasks for phase1 and phase2
    //--------------------------------------------------------------------------

    info = GB_ewise_slice (
        // computed by phase0b
        &TaskList, &max_ntasks, &ntasks, &nthreads,
        // computed by phase0:
        Cnvec, Ch, C_to_M, C_to_A, C_to_B, Ch_is_Mh,
        // original input:
        M, A, B, Context) ;

    if (info != GrB_SUCCESS)
    { 
        // out of memory; free everything allocated by GB_add_phase0
        GB_FREE (Ch) ;
        GB_FREE (C_to_M) ;
        GB_FREE (C_to_A) ;
        GB_FREE (C_to_B) ;
        return (info) ;
    }

    //--------------------------------------------------------------------------
    // phase1: count the number of entries in each vector of C
    //--------------------------------------------------------------------------

    info = GB_add_phase1 (
        // computed or used by phase1:
        &Cp, &Cnvec_nonempty, op == NULL,
        // from phase0b:
        TaskList, ntasks, nthreads,
        // from phase0:
        Cnvec, Ch, C_to_M, C_to_A, C_to_B, Ch_is_Mh,
        // original input:
        M, Mask_struct, A, B, Context) ;

    if (info != GrB_SUCCESS)
    { 
        // out of memory; free everything allocated by GB_add_phase0
        GB_FREE (TaskList) ;
        GB_FREE (Ch) ;
        GB_FREE (C_to_M) ;
        GB_FREE (C_to_A) ;
        GB_FREE (C_to_B) ;
        return (info) ;
    }

    //--------------------------------------------------------------------------
    // phase2: compute the entries (indices and values) in each vector of C
    //--------------------------------------------------------------------------

    // Cp and Ch are either freed by phase2, or transplanted into C.
    // Either way, they are not freed here.

    info = GB_add_phase2 (
        // computed or used by phase2:
        &C, ctype, C_is_csc, op,
        // from phase1:
        Cp, Cnvec_nonempty,
        // from phase0b:
        TaskList, ntasks, nthreads,
        // from phase0:
        Cnvec, Ch, C_to_M, C_to_A, C_to_B, Ch_is_Mh,
        // original input:
        M, Mask_struct, A, B, Context) ;

    // free workspace
    GB_FREE (TaskList) ;
    GB_FREE (C_to_M) ;
    GB_FREE (C_to_A) ;
    GB_FREE (C_to_B) ;

    if (info != GrB_SUCCESS)
    { 
        // out of memory; note that Cp and Ch are already freed
        return (info) ;
    }

    // if successful, Ch and Cp must not be freed; they are now C->h and C->p

    //--------------------------------------------------------------------------
    // return result
    //--------------------------------------------------------------------------

    ASSERT_MATRIX_OK (C, "C output for add", GB0) ;
    (*Chandle) = C ;
    return (GrB_SUCCESS) ;
}

