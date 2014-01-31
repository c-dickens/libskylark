/*
 * BlockADMM.h
 *
 *  Created on: Jan 12, 2014
 *      Author: vikas
 */

#ifndef BLOCKADMM_H_
#define BLOCKADMM_H_

#include <elemental.hpp>
#include <skylark.hpp>
#include <cmath>
#include <boost/mpi.hpp>
#include "hilbert.hpp"

typedef elem::DistMatrix<double, elem::VC, elem::STAR> DistInputMatrixType;
typedef elem::DistMatrix<double, elem::VC, elem::STAR> DistTargetMatrixType;
typedef elem::DistMatrix<double, elem::VC, elem::STAR> DistMatrixType;
typedef elem::Matrix<double> LocalMatrixType;
typedef skylark::sketch::context_t skylark_context_t;

class BlockADMMSolver
{
public:
	BlockADMMSolver(const lossfunction* loss,
					const regularization* regularizer,
					const FeatureTransform* featureMap,
					double lambda, // regularization parameter
					int NumFeatures,
					int NumFeaturePartitions = 1,
					int NumThreads = 1,
					double TOL = 0.1,
					int MAXITER = 1000,
					double RHO = 1.0);
	~BlockADMMSolver();

	void InitializeCache();
	int train(skylark_context_t& context, DistInputMatrixType& X, DistTargetMatrixType& Y, LocalMatrixType& W);

private:
	double lambda;
	double RHO;
	int MAXITER;
	double TOL;
	int NumFeaturePartitions;
	int NumThreads;
	int NumFeatures;
	lossfunction* loss;
	regularization* regularizer;
	FeatureTransform* featureMap;
	LocalMatrixType **Cache;
};


void BlockADMMSolver::InitializeCache() {
	Cache = new LocalMatrixType* [NumFeaturePartitions];
	int start, finish, sj;
	for(int j=0; j<NumFeaturePartitions; j++) {
		start = floor(round(j*NumFeatures*1.0/NumFeaturePartitions));
		finish = floor(round((j+1)*NumFeatures*1.0/NumFeaturePartitions))-1;
		sj = finish - start  + 1;
		Cache[j]  = new elem::Matrix<double>(sj, sj);
	}
}

BlockADMMSolver::BlockADMMSolver(const lossfunction* loss,
		const regularization* regularizer,
		const FeatureTransform* featureMap,
		double lambda,
		int NumFeatures,
		int NumFeaturePartitions,
		int NumThreads,
		double TOL,
		int MAXITER,
		double RHO) {

		this->loss = const_cast<lossfunction *> (loss);
		this->regularizer = const_cast<regularization *> (regularizer);
		this->featureMap = const_cast<FeatureTransform *> (featureMap);
		this->lambda = lambda;
		this->NumFeatures = NumFeatures;
		this->NumFeaturePartitions = NumFeaturePartitions;
		this->NumThreads = NumThreads;
		this->TOL = TOL;
		this->MAXITER = MAXITER;
		this->RHO = RHO;
		InitializeCache();
}




int BlockADMMSolver::train(skylark_context_t& context,  DistInputMatrixType& X, DistTargetMatrixType& Y, LocalMatrixType& Wbar) {

	int P = context.size;

	int n = X.Height();
	int d = X.Width();

	// number of classes, targets - to generalize
	int k = Y.Width();

	// number of random features
	int D = NumFeatures;

	// exception: check if D = Wbar.Height();

	//elem::Grid grid;

	DistMatrixType O(n, k); //uses default Grid
	elem::Zeros(O, n, k);

	DistMatrixType Obar(n, k); //uses default Grid
	elem::Zeros(Obar, n, k);

	DistMatrixType nu(n, k); //uses default Grid
	elem::Zeros(nu, n, k);

	LocalMatrixType W, mu, Wi, mu_ij, ZtObar_ij;

	if(context.rank==0) {
		elem::Zeros(W, D, k);
		elem::Zeros(mu, D, k);
		}
	elem::Zeros(Wi, D, k);
	elem::Zeros(mu_ij, D, k);
	elem::Zeros(ZtObar_ij, D, k);

	int iter = 0;

	int ni = O.LocalHeight();

	elem::Matrix<double> x = X.Matrix();
	elem::Matrix<double> y = Y.Matrix();


	double localloss = loss->evaluate(O.Matrix(), y);
	double totalloss;

	int Dk = D*k;
	int nik  = ni*k;
	int start, finish, sj;

	boost::mpi::timer timer;

	LocalMatrixType sum_o;

	while(iter<MAXITER) {
		iter++;

		reduce(context.comm, localloss, totalloss, std::plus<double>(), 0);
		if(context.rank==0) {
				std::cout << "iteration "<< iter << " total loss:" << totalloss << " ("<< timer.elapsed() << " seconds)" << std::endl;
		}

		broadcast(context.comm, Wbar.Buffer(), Dk, 0);

		//matrixminus(mu_ij.Buffer(), Wbar.Buffer(), Dk);

		elem::Axpy(-1.0, Wbar, mu_ij);

		elem::Axpy(-1.0, nu.Matrix(), Obar.Matrix());

	//	matrixminus(Obar.Buffer(), nu.Buffer(), nik);

		loss->proxoperator(Obar.Matrix(), 1.0/RHO, y, O.Matrix());

		if(context.rank==0) {
			regularizer->proxoperator(Wbar, lambda/RHO, mu, W);
		}

		//LocalMatrixType **Cache = new LocalMatrixType* [NumFeaturePartitions];


		elem::Zeros(sum_o, ni, k);
		elem::Matrix<double> o(ni, k);

		for(int j=0; j<NumFeaturePartitions; j++) {
			start = floor(round(j*D*1.0/NumFeaturePartitions));
			finish = floor(round((j+1)*D*1.0/NumFeaturePartitions))-1;
			sj = finish - start  + 1;

			elem::Matrix<double> z(ni, sj);
			elem::Matrix<double> tmp(sj, k);
			elem::Matrix<double> rhs(sj, k);

    		featureMap->map(x, start, finish, z);



			if(iter==1) {

				elem::Matrix<double> Ones;
				elem::Ones(Ones, sj, 1);
			//elem::Syrk(elem::UPPER, elem::TRANSPOSE, 1.0, z, 0.0, *Cache[j]);
				elem::Gemm(elem::TRANSPOSE, elem::NORMAL, 1.0, z, z, 0.0, *Cache[j]);
				Cache[j]->UpdateDiagonal(Ones);
				//elem::Cholesky(elem::UPPER, *Cache[j]);
				elem::Inverse(*Cache[j]);
				//	Ones.Empty();
			}



			elem::View(tmp, Wbar, start, 0, sj, k); //tmp = Wbar[J,:]
			rhs = tmp; //rhs = Wbar[J,:]
			elem::View(tmp, mu_ij, start, 0, sj, k); //tmp = mu_ij[J,:]
			elem::Axpy(-1.0, tmp, rhs); // rhs = rhs - mu_ij[J,:] = Wbar[J,:] - mu_ij[J,:]
			elem::View(tmp, ZtObar_ij, start, 0, sj, k);
			elem::Axpy(+1.0, tmp, rhs); // rhs = rhs + ZtObar_ij[J,:]
			elem::Gemm(elem::TRANSPOSE, elem::NORMAL, 1.0, z, nu.Matrix(), 1.0, rhs); // rhs = rhs + z'*nu

			elem::View(tmp, Wi, start, 0, sj, k);
		    elem::Gemm(elem::NORMAL, elem::NORMAL, 1.0, *Cache[j], rhs, 0.0, tmp); // ]tmp = Wi[J,:] = Cache[j]*rhs



			elem::Gemm(elem::NORMAL, elem::NORMAL, 1.0, z, tmp, 0.0, o); // o = z*tmp = z*Wi[J,:]

			// mu_ij[JJ,:] = mu_ij[JJ,:] + Wi[JJ,:];
			elem::View(tmp, mu_ij, start, 0, sj, k); //tmp = mu_ij[J,:]
			elem::View(rhs, Wi, start, 0, sj, k);
			elem::Axpy(+1.0, rhs, tmp);

		    //ZtObar_ij[JJ,:] = numpy.dot(Z.T, o);
	 		elem::View(tmp, ZtObar_ij, start, 0, sj, k);
			elem::Gemm(elem::TRANSPOSE, elem::NORMAL, 1.0, z, o, 0.0, tmp);

			//  sum_o += o
			elem::Axpy(1.0, o, sum_o);

			z.Empty();
		}

		localloss = 0.0 ;
	 //	elem::Zeros(o, ni, k);
		elem::MakeZeros(o);
		elem::Scal(-1.0, sum_o);
		elem::Axpy(+1.0, O.Matrix(), sum_o); // sum_o = O.Matrix - sum_o

		for(int j=0; j<NumFeaturePartitions; j++) {
					start = floor(round(j*D*1.0/NumFeaturePartitions));
					finish = floor(round((j+1)*D*1.0/NumFeaturePartitions))-1;
					sj = finish - start  + 1;
					elem::Matrix<double> z(ni, sj);
					elem::Matrix<double> tmp(sj, k);
					featureMap->map(x, start, finish, z);

					elem::View(tmp, ZtObar_ij, start, 0, sj, k);
					elem::Gemm(elem::TRANSPOSE, elem::NORMAL, 1.0/(NumFeaturePartitions + 1.0), z, sum_o, 1.0, tmp);
					elem::View(tmp, Wbar, start, 0, sj, k);
					elem::Gemm(elem::NORMAL, elem::NORMAL, 1.0, z, tmp, 1.0, o);
		}



		localloss+= loss->evaluate(o, y);

		elem::Copy(O.Matrix(), Obar.Matrix());
		elem::Scal(1.0/(NumFeaturePartitions+1.0), sum_o);
		elem::Axpy(-1.0, sum_o, Obar.Matrix());

		elem::Axpy(+1.0, O.Matrix(), nu.Matrix());
		elem::Axpy(-1.0, Obar.Matrix(), nu.Matrix());

		//Wbar = comm.reduce(Wi)

		boost::mpi::reduce (context.comm,
		                        Wi.LockedBuffer(),
		                        Wi.MemorySize(),
		                        Wbar.Buffer(),
		                        std::plus<double>(),
		                        0);

		if(context.rank==0) {
			//Wbar = (Wisum + W)/(P+1)
			elem::Axpy(1.0, W, Wbar);
			elem::Scal(1.0/(P+1), Wbar);

			// mu = mu + W - Wbar;
			elem::Axpy(+1.0, W, mu);
			elem::Axpy(-1.0, Wbar, mu);
		}
		// deleteCache()
		context.comm.barrier();
	}


	return 0;
}




#endif /* BLOCKADMM_H_ */