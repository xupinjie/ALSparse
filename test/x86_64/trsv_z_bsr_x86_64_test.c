/**
 * @brief openspblas trsv csr test
 * @author Zhuoqiang Guo <gzq9425@qq.com>
 */

#include <alphasparse.h>
#include <stdio.h>
#include <mkl.h>
#include <math.h> 
#define BLOCK_SIZE 4
#define ROW_MAJOR false
#define COL_MAJOR true

static void mkl_trsv(const int argc, const char *argv[], const char *file, int thread_num, const MKL_Complex16 alpha, MKL_Complex16 **ret, size_t *size)
{
    MKL_INT m, k, nnz;
    MKL_INT *row_index, *col_index;
    MKL_Complex16 *values;
    mkl_read_coo_z(file, &m, &k, &nnz, &row_index, &col_index, &values);
    if (m != k)
    {
        printf("sparse matrix must be Square matrix but (%d,%d)\n", (int)m, (int)k);
        exit(-1);
    }
    for(MKL_INT i = 0; i < nnz; i++)
    {
        if(row_index[i] == col_index[i])
        {
            values[i].real += 1.0;
            values[i].imag += 1.0;
        }
    }
    size_t size_x = k;
    size_t size_y = m;
    MKL_Complex16 *x = alpha_memalign(sizeof(MKL_Complex16) * size_x, DEFAULT_ALIGNMENT);
    MKL_Complex16 *y = alpha_memalign(sizeof(MKL_Complex16) * size_y, DEFAULT_ALIGNMENT);
    alpha_fill_random_d((double*)x, 1, size_x * 2);

    mkl_set_num_threads(thread_num);

    sparse_operation_t transA = mkl_args_get_transA(argc, argv);
    struct matrix_descr descr = mkl_args_get_matrix_descrA(argc, argv);

    sparse_matrix_t cooA, csrA;
    mkl_sparse_z_create_coo(&cooA, SPARSE_INDEX_BASE_ZERO, m, k, nnz, row_index, col_index, values);
    mkl_sparse_convert_csr(cooA, SPARSE_OPERATION_NON_TRANSPOSE, &csrA);
    alpha_timer_t timer;
    alpha_timing_start(&timer);
    mkl_call_exit(mkl_sparse_z_trsv(transA, alpha, csrA, descr, x, y), "mkl_sparse_c_trsv");
    alpha_timing_end(&timer);
    alpha_timing_elaped_time_print(&timer, "mkl_sparse_c_trsv");
    mkl_sparse_destroy(cooA);
    mkl_sparse_destroy(csrA);

    *ret = y;
    *size = size_y;
    alpha_free(row_index);
    alpha_free(col_index);
    alpha_free(values);
}

static void alpha_trsv(const int argc, const char *argv[], const char *file, int thread_num, const ALPHA_Complex16 alpha, ALPHA_Complex16 **ret, size_t *size, bool flag)
{
    ALPHA_INT m, k, nnz;
    ALPHA_INT *row_index, *col_index;
    ALPHA_Complex16 *values;
    alpha_read_coo_z(file, &m, &k, &nnz, &row_index, &col_index, &values);
    if (m != k)
    {
        printf("sparse matrix must be Square matrix but (%d,%d)\n", (int)m, (int)k);
        exit(-1);
    }
    if (m != k)
    {
        printf("sparse matrix must be Square matrix but (%d,%d)\n", (int)m, (int)k);
        exit(-1);
    }
    size_t size_x = k;
    size_t size_y = m;
    ALPHA_Complex16 *x = alpha_memalign(sizeof(ALPHA_Complex16) * size_x, DEFAULT_ALIGNMENT);
    ALPHA_Complex16 *y = alpha_memalign(sizeof(ALPHA_Complex16) * size_y, DEFAULT_ALIGNMENT);
    for(ALPHA_INT i = 0; i < nnz; i++)
    {
        if(row_index[i] == col_index[i])
        {
            values[i].real += 1.0;
            values[i].imag += 1.0;
        }
    }
    alpha_fill_random_z(x, 1, size_x);

    alphasparse_layout_t layout;
    if(flag == ROW_MAJOR)
        layout = ALPHA_SPARSE_LAYOUT_ROW_MAJOR;
    else
        layout = ALPHA_SPARSE_LAYOUT_COLUMN_MAJOR;
    alpha_set_thread_num(thread_num);
    alphasparse_operation_t transA = alpha_args_get_transA(argc, argv);
    struct alpha_matrix_descr descr = alpha_args_get_matrix_descrA(argc, argv);

    alphasparse_matrix_t cooA, bsrA;
    alpha_call_exit(alphasparse_z_create_coo(&cooA, ALPHA_SPARSE_INDEX_BASE_ZERO, m, k, nnz, row_index, col_index, values), "alphasparse_c_create_coo");
    alpha_call_exit(alphasparse_convert_bsr(cooA, BLOCK_SIZE, layout, ALPHA_SPARSE_OPERATION_NON_TRANSPOSE, &bsrA), "alphasparse_convert_bsr");

    alpha_timer_t timer;
    alpha_timing_start(&timer);

    alpha_call_exit(alphasparse_z_trsv_plain(transA, alpha, bsrA, descr, x, y), "alphasparse_c_trsv_plain");

    alpha_timing_end(&timer);
    alpha_timing_elaped_time_print(&timer, "alphasparse_c_trsv_plain");
    alphasparse_destroy(cooA);
    alphasparse_destroy(bsrA);

    *ret = y;
    *size = size_y;
    alpha_free(row_index);
    alpha_free(col_index);
    alpha_free(values);
}

int main(int argc, const char *argv[])
{
    // args
    args_help(argc, argv);
    const char *file = args_get_data_file(argc, argv);
    int thread_num = args_get_thread_num(argc, argv);
    bool check = args_get_if_check(argc, argv);

    const ALPHA_Complex16 alpha_alpha = {.real = 2.0f, .imag = 2.0f};
    const MKL_Complex16 mkl_alpha = {.real = 2.0f, .imag = 2.0f};

    printf("thread_num : %d\n", thread_num);

    // block row major
    printf("block row major begin\n");
    ALPHA_Complex16 *alpha_y;
    MKL_Complex16 *mkl_y;
    size_t size_alpha_y, size_mkl_y;

    alpha_trsv(argc, argv, file, thread_num, alpha_alpha, &alpha_y, &size_alpha_y, ROW_MAJOR);
    int status = 0;
    if (check)
    {
        mkl_trsv(argc, argv, file, thread_num, mkl_alpha, &mkl_y, &size_mkl_y);
        status = check_d((double*)mkl_y, size_mkl_y*2, (double*)alpha_y, size_alpha_y*2);
        // float mkl_nor2 = 0, alpha_nor2 = 0;
        // float * fmkl_y = (float *)mkl_y, * falpha_y = (float *)alpha_y;
        // for(size_t i = 0; i < size_mkl_y * 2; i++)
        // {
        //     mkl_nor2 += fmkl_y[i] * fmkl_y[i];
        //     alpha_nor2 += falpha_y[i] * falpha_y[i];
        // }
        // printf("mkl norm 2 = %f, openspblas norm2 = %f \n",sqrt(mkl_nor2), sqrt(alpha_nor2));
        alpha_free(mkl_y);
    }

    // block column major
    printf("block column major begin\n");
    ALPHA_Complex16 *alpha_y_col;
    MKL_Complex16 *mkl_y_col;
    size_t size_alpha_y_col, size_mkl_y_col;

    alpha_trsv(argc, argv, file, thread_num, alpha_alpha, &alpha_y_col, &size_alpha_y_col, COL_MAJOR);
    if (check)
    {
        mkl_trsv(argc, argv, file, thread_num, mkl_alpha, &mkl_y_col, &size_mkl_y_col);
        status = check_d((double*)mkl_y_col, size_mkl_y_col*2, (double*)alpha_y_col, size_alpha_y_col*2);
        //for(int i=0; i<size_mkl_y_col; i++)
        {
            //printf("mkl:%f, openspblas:%f\n", mkl_y_col[size_mkl_y_col-2], alpha_y_col[size_mkl_y_col-2]);
            //printf("mkl:%f, openspblas:%f\n", mkl_y_col[size_mkl_y_col-1], alpha_y_col[size_mkl_y_col-1]);
        }

	    alpha_free(mkl_y_col);
    }
    printf("block column major end\n\n\n");

    alpha_free(alpha_y);
    alpha_free(alpha_y_col);

    return status;
}