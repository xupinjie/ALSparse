#include <hip/hip_runtime.h>

#include "alphasparse/handle.h"
#include "alphasparse/compute.h"
#include "alphasparse/spdef.h"
#include "alphasparse/util_dcu.h"
#include "alphasparse/types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "alphasparse/kernel_dcu.h"

__global__ static void spts_syncfree_cuda_executor_bsr_wrt_thread(alphasparse_layout_t block_layout,
                                                                  const ALPHA_INT *bsrRowPtr,
                                                                  const ALPHA_INT *bsrColIdx,
                                                                  const ALPHA_Number *bsrVal,
                                                                  const ALPHA_INT bs,
                                                                  volatile ALPHA_INT *get_value,
                                                                  const ALPHA_INT mb,
                                                                  const ALPHA_Number alpha,
                                                                  const ALPHA_Number *x,
                                                                  ALPHA_Number *y,
                                                                  ALPHA_Number *left_sum,
                                                                  ALPHA_Number *diag)

{
    const ALPHA_INT global_id = hipBlockIdx_x * hipBlockDim_x + hipThreadIdx_x;
    const ALPHA_INT stride    = hipBlockDim_x * hipGridDim_x;
    const ALPHA_INT bs2       = bs * bs;

    ALPHA_INT col, j;
    ALPHA_Number yi;

    for (ALPHA_INT i = global_id; i < mb; i += stride) // One thread process one bsr row
    {
        j = bsrRowPtr[mb - 1 - i];
        while (j < bsrRowPtr[mb - i]) // Traversing the cols of BSR
        {
            col = bsrColIdx[j];

            if (col == mb - 1 - i) {
                for (ALPHA_INT blkr = bs - 1; blkr >= 0; blkr--) {
                    for (ALPHA_INT blkc = bs - 1; blkc >= 0; blkc--) {
                        if (blkc == blkr) // process diagonals in block
                        {
                            diag[(mb - 1 - i) * bs + blkr] = bsrVal[j * bs2 + blkr * bs + blkc];
                            break;
                        }
                    }
                }
                //diag[r] = csrVal[ai];
            }
            j++;
        }
    }

    for (ALPHA_INT i = global_id; i < mb; i += stride) // One thread process one bsr row
    {
        ALPHA_INT ii = mb - 1 - i;
        j            = bsrRowPtr[mb - i] - 1;
        while (j >= bsrRowPtr[ii]) // Traversing the cols of BSR
        {
            col = bsrColIdx[j];

            while (get_value[col] == 1) // Waiting for dependent nodes
            {
                if (col > ii) {
                    // process blocks in upper triangle
                    for (ALPHA_INT blkr = 0; blkr < bs; blkr++) {
                        for (ALPHA_INT blkc = 0; blkc < bs; blkc++) {
                            if (block_layout == ALPHA_SPARSE_LAYOUT_ROW_MAJOR) {
                                alpha_madde(left_sum[ii * bs + blkr], bsrVal[j * bs2 + blkr * bs + blkc], y[col * bs + blkc]);
                            } else {
                                alpha_madde(left_sum[ii * bs + blkr], bsrVal[j * bs2 + blkc * bs + blkr], y[col * bs + blkc]);
                            }
                        }
                    }
                } else
                    break;
                j--;
                col = bsrColIdx[j];
            }

            ALPHA_INT tmp_try = !(ii ^ col);
            //if (i == col)
            {
                //ALPHA_INT tmp_try = !(i ^ col);
                for (ALPHA_INT blkr = bs - 1; blkr >= 0; blkr--) {
                    for (ALPHA_INT blkc = bs - 1; blkc >= 0; blkc--) {
                        if (blkc == blkr) // process diagonals in block
                        {
                            ALPHA_Number tmp;
                            alpha_mul(tmp, alpha, x[ii * bs + blkr]);
                            alpha_sub(yi, tmp, left_sum[ii * bs + blkr]);
                            alpha_div(yi, yi, diag[ii * bs + blkr]);
                            alpha_cross_entropy(y[ii * bs + blkr], yi, y[ii * bs + blkr], tmp_try);
                        } else // process lower triangle elems in block
                        {
                            ALPHA_Number tmp;
                            if (block_layout == ALPHA_SPARSE_LAYOUT_ROW_MAJOR) {
                                alpha_mul(tmp, bsrVal[j * bs2 + blkr * bs + blkc], y[col * bs + blkc]);
                            } else {
                                alpha_mul(tmp, bsrVal[j * bs2 + blkc * bs + blkr], y[col * bs + blkc]);
                            }
                            alpha_add(tmp, left_sum[ii * bs + blkr], tmp);
                            //alpha_div(yi, tmp, diag[ii])
                            alpha_cross_entropy(left_sum[ii * bs + blkr], tmp, left_sum[ii * bs + blkr], tmp_try);
                        }
                    }
                }

                get_value[ii] = tmp_try | get_value[ii];
                __threadfence();

                if (tmp_try)
                    break;
            }
        }
    }
}

alphasparse_status_t
ONAME(alphasparse_dcu_handle_t handle,
      alphasparse_layout_t dir,
      ALPHA_INT mb,
      ALPHA_INT nnzb,
      const ALPHA_Number alpha,
      const ALPHA_Number *bsr_val,
      const ALPHA_INT *bsr_row_ptr,
      const ALPHA_INT *bsr_col_ind,
      ALPHA_INT bsr_dim,
      alphasparse_dcu_mat_info_t info,
      const ALPHA_Number *x,
      ALPHA_Number *y,
      alphasparse_dcu_solve_policy_t policy,
      void *temp_buffer)
{
    ALPHA_Number *diag;
    hipMalloc((void **)&diag, sizeof(ALPHA_Number) * mb * bsr_dim);
    hipMemset(diag, '\0', sizeof(ALPHA_Number) * mb * bsr_dim);

    ALPHA_INT *get_value;
    hipMalloc((void **)&get_value, (mb) * sizeof(ALPHA_INT));
    hipMemset(get_value, 0, sizeof(ALPHA_INT) * mb);

    ALPHA_Number *temp;
    hipMalloc((void **)&temp, (mb * bsr_dim) * sizeof(ALPHA_Number));
    hipMemset(temp, 0, sizeof(ALPHA_Number) * mb * bsr_dim);

    ALPHA_INT block_size = 512;
    ALPHA_INT block_num  = (mb - 1) / block_size + 1;

    hipLaunchKernelGGL(spts_syncfree_cuda_executor_bsr_wrt_thread, dim3(block_num), dim3(block_size), 0, handle->stream, dir, bsr_row_ptr, bsr_col_ind, bsr_val, bsr_dim, get_value, mb, alpha, x, y, temp, diag);
    hipFree(diag);
    hipFree(get_value);
    hipFree(temp);
    return ALPHA_SPARSE_STATUS_SUCCESS;
}

#ifdef __cplusplus
}
#endif /*__cplusplus */