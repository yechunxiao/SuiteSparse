//------------------------------------------------------------------------------
// GB_dense_subassign_06d: C(:,:)<A> = A; C is dense, and M and A are aliased
//------------------------------------------------------------------------------

// SuiteSparse:GraphBLAS, Timothy A. Davis, (c) 2017-2020, All Rights Reserved.
// http://suitesparse.com   See GraphBLAS/Doc/License.txt for license.

//------------------------------------------------------------------------------

// Method 06d: C(:,:)<A> = A ; no S, C is dense, M and A are aliased

// M:           present
// Mask_comp:   false
// C_replace:   false
// accum:       NULL
// A:           matrix, and aliased to M
// S:           none

#include "GB_subassign_methods.h"
#include "GB_dense.h"
#ifndef GBCOMPACT
#include "GB_type__include.h"
#endif

#undef  GB_FREE_WORK
#define GB_FREE_WORK \
    GB_ek_slice_free (&pstart_slice, &kfirst_slice, &klast_slice) ;

#undef  GB_FREE_ALL
#define GB_FREE_ALL GB_FREE_WORK

GrB_Info GB_dense_subassign_06d
(
    GrB_Matrix C,
    // input:
    const GrB_Matrix A,
    const bool Mask_struct,
    GB_Context Context
)
{

    //--------------------------------------------------------------------------
    // get inputs
    //--------------------------------------------------------------------------

    GrB_Info info ;
    ASSERT (GB_is_dense (C)) ;
    ASSERT (!GB_PENDING (C)) ; ASSERT (!GB_ZOMBIES (C)) ;
    ASSERT (!GB_PENDING (A)) ; ASSERT (!GB_ZOMBIES (A)) ;
    ASSERT_MATRIX_OK (C, "C for subassign method_06d", GB0) ;
    ASSERT_MATRIX_OK (A, "A for subassign method_06d", GB0) ;
    const GB_Type_code ccode = C->type->code ;

    //--------------------------------------------------------------------------
    // Method 06d: C(:,:)<A> = A ; no S; C is dense, M and A are aliased
    //--------------------------------------------------------------------------

    // Time: Optimal:  the method must iterate over all entries in A,
    // and the time is O(nnz(A)).

    //--------------------------------------------------------------------------
    // Parallel: slice A into equal-sized chunks
    //--------------------------------------------------------------------------

    GB_GET_NTHREADS_MAX (nthreads_max, chunk, Context) ;
    int nthreads = GB_nthreads (GB_NNZ (A) + A->nvec, chunk, nthreads_max) ;
    int ntasks = (nthreads == 1) ? 1 : (8 * nthreads) ;
    ntasks = GB_IMIN (ntasks, GB_NNZ (A)) ;
    ntasks = GB_IMAX (ntasks, 1) ;

    //--------------------------------------------------------------------------
    // slice the entries for each task
    //--------------------------------------------------------------------------

    // Task tid does entries pstart_slice [tid] to pstart_slice [tid+1]-1 and
    // vectors kfirst_slice [tid] to klast_slice [tid].  The first and last
    // vectors may be shared with prior slices and subsequent slices.

    int64_t *pstart_slice = NULL, *kfirst_slice = NULL, *klast_slice = NULL ;
    if (!GB_ek_slice (&pstart_slice, &kfirst_slice, &klast_slice, A, ntasks))
    { 
        // out of memory
        return (GB_OUT_OF_MEMORY) ;
    }

    //--------------------------------------------------------------------------
    // C<A> = A for built-in types
    //--------------------------------------------------------------------------

    bool done = false ;

    #ifndef GBCOMPACT

        //----------------------------------------------------------------------
        // define the worker for the switch factory
        //----------------------------------------------------------------------

        #define GB_Cdense_06d(cname) GB_Cdense_06d_ ## cname

        #define GB_WORKER(cname)                                              \
        {                                                                     \
            info = GB_Cdense_06d(cname) (C, A, Mask_struct,                   \
                kfirst_slice, klast_slice, pstart_slice, ntasks, nthreads) ;  \
            done = (info != GrB_NO_VALUE) ;                                   \
        }                                                                     \
        break ;

        //----------------------------------------------------------------------
        // launch the switch factory
        //----------------------------------------------------------------------

        if (C->type == A->type && ccode < GB_UDT_code)
        { 
            // C<A> = A
            switch (ccode)
            {
                case GB_BOOL_code   : GB_WORKER (_bool  )
                case GB_INT8_code   : GB_WORKER (_int8  )
                case GB_INT16_code  : GB_WORKER (_int16 )
                case GB_INT32_code  : GB_WORKER (_int32 )
                case GB_INT64_code  : GB_WORKER (_int64 )
                case GB_UINT8_code  : GB_WORKER (_uint8 )
                case GB_UINT16_code : GB_WORKER (_uint16)
                case GB_UINT32_code : GB_WORKER (_uint32)
                case GB_UINT64_code : GB_WORKER (_uint64)
                case GB_FP32_code   : GB_WORKER (_fp32  )
                case GB_FP64_code   : GB_WORKER (_fp64  )
                case GB_FC32_code   : GB_WORKER (_fc32  )
                case GB_FC64_code   : GB_WORKER (_fc64  )
                default: ;
            }
        }

    #endif

    //--------------------------------------------------------------------------
    // C<A> = A for user-defined types, and typecasting
    //--------------------------------------------------------------------------

    if (!done)
    { 

        //----------------------------------------------------------------------
        // get operators, functions, workspace, contents of A and C
        //----------------------------------------------------------------------

        GB_BURBLE_MATRIX (A, "generic ") ;

        const size_t csize = C->type->size ;
        const size_t asize = A->type->size ;
        const GB_Type_code acode = A->type->code ;
        GB_cast_function cast_A_to_C = GB_cast_factory (ccode, acode) ;

        // Cx [p] = (ctype) Ax [pA]
        #define GB_COPY_A_TO_C(Cx,p,Ax,pA) \
            cast_A_to_C (Cx + ((p)*csize), Ax + ((pA)*asize), asize)

        #define GB_AX_MASK(Ax,pA,asize) \
            GB_mcast (Ax, pA, asize)

        #define GB_CTYPE GB_void
        #define GB_ATYPE GB_void

        // no vectorization
        #define GB_PRAGMA_SIMD_VECTORIZE ;

        #include "GB_dense_subassign_06d_template.c"
    }

    //--------------------------------------------------------------------------
    // free workspace and return result
    //--------------------------------------------------------------------------

    GB_FREE_WORK ;
    ASSERT_MATRIX_OK (C, "C output for subassign method_06d", GB0) ;
    return (GrB_SUCCESS) ;
}

