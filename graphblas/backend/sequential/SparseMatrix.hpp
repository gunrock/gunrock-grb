#ifndef GRB_BACKEND_SEQUENTIAL_SPARSEMATRIX_HPP
#define GRB_BACKEND_SEQUENTIAL_SPARSEMATRIX_HPP

#include <vector>
#include <iostream>
#include <typeinfo>

#include <cuda.h>
#include <cuda_runtime_api.h>

#include "graphblas/backend/sequential/Matrix.hpp"
#include "graphblas/backend/sequential/sequential.hpp"
#include "graphblas/backend/sequential/util.hpp"
#include "graphblas/util.hpp"

namespace graphblas
{
namespace backend
{
  template <typename T>
  class DenseMatrix;

  template <typename T>
  class SparseMatrix
  {
    public:
    SparseMatrix()
			 	: nrows_(0), ncols_(0), nvals_(0), 
				h_csrColInd(NULL), h_csrRowPtr(NULL), h_csrVal(NULL) {}

    SparseMatrix( const Index nrows, const Index ncols )
			  : nrows_(nrows), ncols_(ncols), nvals_(0),
				h_csrColInd(NULL), h_csrRowPtr(NULL), h_csrVal(NULL) {}

	  // C API Methods
	  Info build( const std::vector<Index>& row_indices,
		        		const std::vector<Index>& col_indices,
				        const std::vector<T>& values,
				        const Index nvals,
				        const SparseMatrix& mask,
				        const BinaryOp& dup );

	  Info build( const std::vector<Index>& row_indices,
		        		const std::vector<Index>& col_indices,
				        const std::vector<T>& values,
				        const Index nvals );

    Info extractTuples( std::vector<Index>& row_indices,
                        std::vector<Index>& col_indices,
                        std::vector<T>&     values ) const;

		// Mutators
		// private method for setting nrows and ncols
    Info nnew( const Index nrows, const Index ncols );
	  // private method for allocation
		Info allocate();	
    Info clear();
    Info print(); 
    Info printCSR( const char* str ); // private method for pretty printing

		// Accessors
		Info nrows( Index& nrows ) const;
		Info ncols( Index& ncols ) const;
		Info nvals( Index& nvals ) const;

    private:
    Index nrows_;
    Index ncols_;
    Index nvals_;

		// CSR format
    Index* h_csrColInd;
    Index* h_csrRowPtr;
    T*     h_csrVal;

    // CSC format
		// TODO: add CSC support. 
		// -this will be useful and necessary for direction-optimized SpMV
		/*Index* h_cscRowInd;
		Index* h_cscColPtr;
    T*     h_cscVal;*/

    // TODO: add sequential single-threaded spmm
		template <typename c, typename a, typename b>
		friend Info spmm( DenseMatrix<c>&        C,
                      const Semiring&        op,
                      const SparseMatrix<a>& A,
                      const DenseMatrix<b>&  B );

		// TODO: add sequential single-threaded spmm
		// For testing
		template <typename c, typename a, typename b>
		friend Info spmm( DenseMatrix<c>&        C,
                      const Semiring&        op,
                      const SparseMatrix<a>& A,
                      const DenseMatrix<b>&  B,
					            const int TA,
					            const int TB,
					            const int NT,
					            const bool ROW_MAJOR );

		// TODO: add mkl_spmm
		//template <typename c, typename a, typename b>
		//friend Info mkl_spmm( DenseMatrix<c>&        C,
    //                      const Semiring&        op,
    //                      const SparseMatrix<a>& A,
    //                      const DenseMatrix<b>&  B );

		template <typename c, typename a, typename b>
		friend Info mkl_spgemm( SparseMatrix<c>&       C,
                            const Semiring&        op,
                            const SparseMatrix<a>& A,
                            const SparseMatrix<b>& B );

		template <typename c, typename a, typename b>
		friend Info mkl_spgemm_analyze( SparseMatrix<c>&       C,
                            const Semiring&        op,
                            const SparseMatrix<a>& A,
                            const SparseMatrix<b>& B );

		template <typename c, typename a, typename b>
		friend Info mkl_spgemm_compute( SparseMatrix<c>&       C,
                            const Semiring&        op,
                            const SparseMatrix<a>& A,
                            const SparseMatrix<b>& B );
  };

  template <typename T>
  Info SparseMatrix<T>::build( const std::vector<Index>& row_indices,
                               const std::vector<Index>& col_indices,
                               const std::vector<T>& values,
                               const Index nvals,
                               const SparseMatrix& mask,
                               const BinaryOp& dup) {}

  template <typename T>
  Info SparseMatrix<T>::build( const std::vector<Index>& row_indices,
                               const std::vector<Index>& col_indices,
                               const std::vector<T>& values,
                               const Index nvals )
	{
    nvals_ = nvals;

    allocate();

    // Convert to CSR/CSC
    Index temp, row, col, dest, cumsum=0;

    // Set all rowPtr to 0
    for( Index i=0; i<=nrows_; i++ )
      h_csrRowPtr[i] = 0;
    // Go through all elements to see how many fall in each row
    for( Index i=0; i<nvals_; i++ ) {
			row = row_indices[i];
		  if( row>=nrows_ ) return GrB_INDEX_OUT_OF_BOUNDS;
      h_csrRowPtr[ row ]++;
		}
    // Cumulative sum to obtain rowPtr
    for( Index i=0; i<nrows_; i++ ) {
      temp = h_csrRowPtr[i];
      h_csrRowPtr[i] = cumsum;
      cumsum += temp;
    }
    h_csrRowPtr[nrows_] = nvals;

    // Store colInd and val
    for( Index i=0; i<nvals_; i++ ) {
      row = row_indices[i];
      dest= h_csrRowPtr[row];
			col = col_indices[i];
			if( col>=ncols_ ) return GrB_INDEX_OUT_OF_BOUNDS;
      h_csrColInd[dest] = col;
      h_csrVal[dest]    = values[i];
      h_csrRowPtr[row]++;
    }
    cumsum = 0;
    
    // Undo damage done to rowPtr
    for( Index i=0; i<=nrows_; i++ ) {
      temp = h_csrRowPtr[i];
      h_csrRowPtr[i] = cumsum;
      cumsum = temp;
    }

		return GrB_SUCCESS;
	}

	template <typename T>
  Info SparseMatrix<T>::extractTuples( std::vector<Index>& row_indices,
                                       std::vector<Index>& col_indices,
                                       std::vector<T>&     values ) const
	{
    row_indices.clear();
		col_indices.clear();
		values.clear();

		for( Index row=0; row<nrows_; row++ ) {
		  for( Index ind=h_csrRowPtr[row]; ind<h_csrRowPtr[row+1]; ind++ ) {
        row_indices.push_back(row);
				col_indices.push_back(h_csrColInd[ind]);
				values.push_back(     h_csrVal[ind]);
			}
		}

		return GrB_SUCCESS;
	}

	template <typename T>
	Info SparseMatrix<T>::nnew( const Index nrows, const Index ncols )
	{
		nrows_ = nrows;
		ncols_ = ncols;
		return GrB_SUCCESS;
	}

	template <typename T>
	Info SparseMatrix<T>::allocate()
	{
    // Host malloc
    if( nrows_!=0 && h_csrRowPtr == NULL ) 
			h_csrRowPtr = (Index*)malloc((nrows_+1)*sizeof(Index));
    if( nvals_!=0 && h_csrColInd == NULL )
			h_csrColInd = (Index*)malloc(nvals_*sizeof(Index));
    if( nvals_!=0 && h_csrVal == NULL )
			h_csrVal    = (T*)    malloc(nvals_*sizeof(T));

	 	if( h_csrRowPtr==NULL ) return GrB_OUT_OF_MEMORY;
	 	if( h_csrColInd==NULL ) return GrB_OUT_OF_MEMORY;
	 	if( h_csrVal==NULL )    return GrB_OUT_OF_MEMORY;

		return GrB_SUCCESS;
	}

  template <typename T>
	Info SparseMatrix<T>::clear()
	{
    if( h_csrRowPtr ) free( h_csrRowPtr );
    if( h_csrColInd ) free( h_csrColInd );
    if( h_csrVal )    free( h_csrVal );
    return GrB_SUCCESS;
	}

  template <typename T>
  Info SparseMatrix<T>::print()
	{
    printArray( "csrColInd", h_csrColInd );
		printArray( "csrRowPtr", h_csrRowPtr );
		printArray( "csrVal",    h_csrVal );
		printCSR( "pretty print" );
		return GrB_SUCCESS;
	}

	template <typename T>
	Info SparseMatrix<T>::printCSR( const char* str )
	{
		Index length = std::min(20, nrows_);
    std::cout << str << ":\n";

		for( Index row=0; row<length; row++ ) {
      Index col_start = h_csrRowPtr[row];
			Index col_end   = h_csrRowPtr[row+1];
			for( Index col=0; col<length; col++ ) {
				Index col_ind = h_csrColInd[col_start];
				if( col_start<col_end && col_ind==col ) {
					std::cout << "x ";
					col_start++;
				} else {
					std::cout << "0 ";
				}
			}
			std::cout << std::endl;
		}
		return GrB_SUCCESS;
	}

	template <typename T>
	Info SparseMatrix<T>::nrows( Index& nrows ) const
	{
		nrows = nrows_;
		return GrB_SUCCESS;
	}

	template <typename T>
	Info SparseMatrix<T>::ncols( Index& ncols ) const
	{
		ncols = ncols_;
		return GrB_SUCCESS;
	}

	template <typename T>
	Info SparseMatrix<T>::nvals( Index& nvals ) const
	{
		nvals = nvals_;
		return GrB_SUCCESS;
	}
} // backend
} // graphblas

#endif  // GRB_BACKEND_SEQUENTIAL_SPARSEMATRIX_HPP