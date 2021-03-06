/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 1999-2008 Gunnar Raetsch
 * Written (W) 2007-2009 Soeren Sonnenburg
 * Copyright (C) 1999-2009 Fraunhofer Institute FIRST and Max-Planck-Society
 */

#include <shogun/clustering/KMeansLloydImpl.h>
#include "shogun/clustering/KMeansMiniBatchImpl.h"
#include <shogun/clustering/KMeans.h>
#include <shogun/distance/Distance.h>
#include <shogun/distance/EuclideanDistance.h>
#include <shogun/labels/Labels.h>
#include <shogun/features/DenseFeatures.h>
#include <shogun/mathematics/Math.h>
#include <shogun/base/Parallel.h>
#include <shogun/mathematics/eigen3.h>

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#ifdef HAVE_LINALG_LIB
#include <shogun/mathematics/linalg/linalg.h>
#endif

using namespace shogun;
using namespace Eigen;

CKMeans::CKMeans()
: CDistanceMachine()
{
	init();
}

CKMeans::CKMeans(int32_t k_, CDistance* d, EKMeansMethod f)
:CDistanceMachine()
{
	init();
	k=k_;
	set_distance(d);
	train_method=f;
}

CKMeans::CKMeans(int32_t k_, CDistance* d, bool use_kmpp, EKMeansMethod f)
: CDistanceMachine()
{
	init();
	k=k_;
	set_distance(d);
	use_kmeanspp=use_kmpp;
	train_method=f;
}

CKMeans::CKMeans(int32_t k_i, CDistance* d_i, SGMatrix<float64_t> centers_i, EKMeansMethod f)
: CDistanceMachine()
{
	init();
	k = k_i;
	set_distance(d_i);
	set_initial_centers(centers_i);
	train_method=f;
}

CKMeans::~CKMeans()
{
}

void CKMeans::set_initial_centers(SGMatrix<float64_t> centers)
{
 	CDenseFeatures<float64_t>* lhs=((CDenseFeatures<float64_t>*) distance->get_lhs());
	dimensions=lhs->get_num_features();
	REQUIRE(centers.num_cols == k,
			"Expected %d initial cluster centers, got %d.\n", k, centers.num_cols);
	REQUIRE(centers.num_rows == dimensions,
			"Expected %d dimensionional cluster centers, got %d.\n", dimensions, centers.num_rows);
	mus_initial = centers;
	SG_UNREF(lhs);
}

void CKMeans::set_random_centers()
{
	mus.zero();
	CDenseFeatures<float64_t>* lhs=
		CDenseFeatures<float64_t>::obtain_from_generic(distance->get_lhs());
	int32_t lhs_size=lhs->get_num_vectors();
	
	SGVector<int32_t> temp=SGVector<int32_t>(lhs_size);
	SGVector<int32_t>::range_fill_vector(temp, lhs_size, 0);
	CMath::permute(temp);

	for (int32_t i=0; i<k; i++)
	{
		const int32_t cluster_center_i=temp[i];
		SGVector<float64_t> vec=lhs->get_feature_vector(cluster_center_i);

		for (int32_t j=0; j<dimensions; j++)
			mus(j,i)=vec[j];

		lhs->free_feature_vector(vec, cluster_center_i);
	}

	SG_UNREF(lhs);

}

void CKMeans::compute_cluster_variances()
{
	/* compute the ,,variances'' of the clusters */
	for (int32_t i=0; i<k; i++)
	{
		float64_t rmin1=0;
		float64_t rmin2=0;

		bool first_round=true;

		for (int32_t j=0; j<k; j++)
		{
			if (j!=i)
			{
				int32_t l;
				float64_t dist = 0;

				for (l=0; l<dimensions; l++)
				{
					dist+=CMath::sq(
							mus.matrix[i*dimensions+l]
									-mus.matrix[j*dimensions+l]);
				}

				if (first_round)
				{
					rmin1=dist;
					rmin2=dist;
					first_round=false;
				}
				else
				{
					if ((dist<rmin2) && (dist>=rmin1))
						rmin2=dist;

					if (dist<rmin1)
					{
						rmin2=rmin1;
						rmin1=dist;
					}
				}
			}
		}

		R.vector[i]=(0.7*CMath::sqrt(rmin1)+0.3*CMath::sqrt(rmin2));
	}
}

bool CKMeans::train_machine(CFeatures* data)
{
	REQUIRE(distance, "Distance is not provided.\n")
	REQUIRE(distance->get_feature_type()==F_DREAL,
			"Distance's features type (%d) should be of type REAL (%d).\n")
	
	if (data)
		distance->init(data, data);

	CDenseFeatures<float64_t>* lhs=
		CDenseFeatures<float64_t>::obtain_from_generic(distance->get_lhs());

	REQUIRE(lhs, "Lhs features of distance not provided.\n");
	int32_t lhs_size=lhs->get_num_vectors();
	dimensions=lhs->get_num_features();
	const int32_t centers_size=dimensions*k;

	REQUIRE(lhs_size>0, "Lhs features should not be empty.\n");
	REQUIRE(dimensions>0, "Lhs features dimensions (%d) should be >0.\n", dimensions);

	/* if kmeans++ to be used */
	if (use_kmeanspp)
	{
#ifdef HAVE_LINALG_LIB
		mus_initial=kmeanspp();
#endif
	}

	R=SGVector<float64_t>(k);

	mus=SGMatrix<float64_t>(dimensions, k);
	/* cluster_centers=zeros(dimensions, k) ; */
	memset(mus.matrix, 0, sizeof(float64_t)*centers_size);


	if (mus_initial.matrix)
		mus = mus_initial;
	else
		set_random_centers();

	if (train_method==KMM_MINI_BATCH)
	{
		CKMeansMiniBatchImpl::minibatch_KMeans(k, distance, batch_size, minib_iter, mus);
	}
	else
	{
		CKMeansLloydImpl::Lloyd_KMeans(k, distance, max_iter, mus, fixed_centers);
	}

	compute_cluster_variances();
	SG_UNREF(lhs);
	return true;
}

bool CKMeans::load(FILE* srcfile)
{
	SG_SET_LOCALE_C;
	SG_RESET_LOCALE;
	return false;
}

bool CKMeans::save(FILE* dstfile)
{
	SG_SET_LOCALE_C;
	SG_RESET_LOCALE;
	return false;
}

void CKMeans::set_use_kmeanspp(bool kmpp)
{
	use_kmeanspp=kmpp;
}

bool CKMeans::get_use_kmeanspp() const
{
	return use_kmeanspp;
}

void CKMeans::set_k(int32_t p_k)
{
	REQUIRE(p_k>0, "Number of clusters (%d) should be > 0\n", k);
	this->k=p_k;
}

int32_t CKMeans::get_k()
{
	return k;
}

void CKMeans::set_max_iter(int32_t iter)
{
	REQUIRE(iter>0, "Maximum number of iterations (%d) should be > 0.\n", iter);
	max_iter=iter;
}

float64_t CKMeans::get_max_iter()
{
	return max_iter;
}

void CKMeans::set_train_method(EKMeansMethod f)
{
	train_method=f;
}

EKMeansMethod CKMeans::get_train_method() const
{
	return train_method;
}

void CKMeans::set_mini_batch_size(int32_t b)
{
	REQUIRE(b>0, "Batch size (%d) should be > 0.\n", b);
	batch_size=b;
}

int32_t CKMeans::get_mini_batch_size() const
{
	return batch_size;
}

void CKMeans::set_mini_batch_num_iterations(int32_t i)
{
	REQUIRE(i>0, "Number of iterations (%d) should be > 0.\n", i);
	minib_iter=i;
}

int32_t CKMeans::get_mini_batch_num_iterations() const
{
	return minib_iter;
}

void CKMeans::set_mini_batch_parameters(int32_t b, int32_t t)
{
	REQUIRE(b>0, "Bach size (%d) should be > 0.\n", b);
	REQUIRE(t>0, "Number of iterations (%d) should be > 0.\n", t);
	batch_size=b;
	minib_iter=t;
}

SGVector<float64_t> CKMeans::get_radiuses()
{
	return R;
}

SGMatrix<float64_t> CKMeans::get_cluster_centers()
{
	if (!R.vector)
		return SGMatrix<float64_t>();

	CDenseFeatures<float64_t>* lhs=
		(CDenseFeatures<float64_t>*)distance->get_lhs();
	SGMatrix<float64_t> centers=lhs->get_feature_matrix();
	SG_UNREF(lhs);
	return centers;
}

int32_t CKMeans::get_dimensions()
{
	return dimensions;
}

void CKMeans::set_fixed_centers(bool fixed)
{
	fixed_centers=fixed;
}

bool CKMeans::get_fixed_centers()
{
	return fixed_centers;
}

void CKMeans::store_model_features()
{
	/* set lhs of underlying distance to cluster centers */
	CDenseFeatures<float64_t>* cluster_centers=new CDenseFeatures<float64_t>(
			mus);

	/* store cluster centers in lhs of distance variable */
	CFeatures* rhs=distance->get_rhs();
	distance->init(cluster_centers, rhs);
	SG_UNREF(rhs);
}

SGMatrix<float64_t> CKMeans::kmeanspp()
{
	int32_t lhs_size;
	CDenseFeatures<float64_t>* lhs=(CDenseFeatures<float64_t>*)distance->get_lhs();
	lhs_size=lhs->get_num_vectors();
	
	SGMatrix<float64_t> centers=SGMatrix<float64_t>(dimensions, k);
	centers.zero();
	SGVector<float64_t> min_dist=SGVector<float64_t>(lhs_size);
	min_dist.zero();

	/* First center is chosen at random */
	int32_t mu=CMath::random((int32_t) 0, lhs_size-1);
	SGVector<float64_t> mu_first=lhs->get_feature_vector(mu);	
	for(int32_t j=0; j<dimensions; j++)
		centers(j, 0)=mu_first[j];

	distance->precompute_lhs();
	distance->precompute_rhs();
#pragma omp parallel for shared(min_dist)
	for(int32_t i=0; i<lhs_size; i++)
		min_dist[i]=CMath::sq(distance->distance(i, mu));
#ifdef HAVE_LINALG
	float64_t sum=linalg::vector_sum(min_dist);
#else //HAVE_LINALG
	Map<VectorXd> eigen_min_dist(min_dist.vector, min_dist.vlen);
	float64_t sum=eigen_min_dist.sum();
#endif //HAVE_LINALG
	int32_t n_rands=2 + int32_t(CMath::log(k));

	/* Choose centers with weighted probability */
	for(int32_t i=1; i<k; i++)
	{	
		int32_t best_center=0;		
		float64_t best_sum=-1.0;
		SGVector<float64_t> best_min_dist=SGVector<float64_t>(lhs_size);

		/* local tries for best center */
		for(int32_t trial=0; trial<n_rands; trial++)
		{
			float64_t temp_sum=0.0;		
			float64_t temp_dist=0.0;
			SGVector<float64_t> temp_min_dist=SGVector<float64_t>(lhs_size);		
			int32_t new_center=0;		
			float64_t prob=CMath::random(0.0, 1.0);
			prob=prob*sum;
		
			for(int32_t j=0; j<lhs_size; j++)
			{
				temp_sum+=min_dist[j];
				if (prob <= temp_sum)
				{
					new_center=j;
					break;
				}
			}

#pragma omp parallel for firstprivate(lhs_size) \
			shared(temp_min_dist)		
			for(int32_t j=0; j<lhs_size; j++)
			{
				temp_dist=CMath::sq(distance->distance(j, new_center));
				temp_min_dist[j]=CMath::min(temp_dist, min_dist[j]);
			}

#ifdef HAVE_LINALG
			temp_sum=linalg::vector_sum(temp_min_dist);
#else //HAVE_LINALG
			Map<VectorXd> eigen_temp_sum(temp_min_dist.vector, temp_min_dist.vlen);
			temp_sum=eigen_temp_sum.sum();
#endif //HAVE_LINALG
			if ((temp_sum<best_sum) || (best_sum<0))
			{
				best_sum=temp_sum;
				best_min_dist=temp_min_dist;
				best_center=new_center;
			}
		}
		
		SGVector<float64_t> vec=lhs->get_feature_vector(best_center);
		for(int32_t j=0; j<dimensions; j++)
			centers(j, i)=vec[j];
		sum=best_sum;
		min_dist=best_min_dist;
	}

	distance->reset_precompute();
	SG_UNREF(lhs);
	return centers;
}

void CKMeans::init()
{
	max_iter=10000;
	k=3;
	dimensions=0;
	fixed_centers=false;
	use_kmeanspp=false;
	train_method=KMM_LLOYD;
	batch_size=-1;
	minib_iter=-1;
	SG_ADD(&max_iter, "max_iter", "Maximum number of iterations", MS_AVAILABLE);
	SG_ADD(&k, "k", "k, the number of clusters", MS_AVAILABLE);
	SG_ADD(&dimensions, "dimensions", "Dimensions of data", MS_NOT_AVAILABLE);
	SG_ADD(&R, "R", "Cluster radiuses", MS_NOT_AVAILABLE);
}

