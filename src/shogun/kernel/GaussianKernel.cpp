/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 1999-2010 Soeren Sonnenburg
 * Written (W) 2011 Abhinav Maurya
 * Written (W) 2012 Heiko Strathmann
 * Copyright (C) 1999-2009 Fraunhofer Institute FIRST and Max-Planck-Society
 * Copyright (C) 2010 Berlin Institute of Technology
 */

#include <shogun/lib/common.h>
#include <shogun/base/Parameter.h>
#include <shogun/kernel/GaussianKernel.h>
#include <shogun/features/DotFeatures.h>
#include <shogun/io/SGIO.h>
#include <shogun/mathematics/Math.h>

using namespace shogun;

void CGaussianKernel::set_width(float64_t w)
{
	REQUIRE(w>0, "width (%f) must be positive\n",w);
	m_log_width=CMath::log(w/2.0)/2.0;
}

float64_t CGaussianKernel::get_width() const
{
	return CMath::exp(m_log_width*2.0)*2.0;
}

CGaussianKernel::CGaussianKernel() : CDotKernel()
{
	init();
}

CGaussianKernel::CGaussianKernel(float64_t w) : CDotKernel()
{
	init();
	set_width(w);
}

CGaussianKernel::CGaussianKernel(int32_t size, float64_t w) : CDotKernel(size)
{
	init();
	set_width(w);
}

CGaussianKernel::CGaussianKernel(CDotFeatures* l, CDotFeatures* r,
		float64_t w, int32_t size) : CDotKernel(size)
{
	init();
	set_width(w);
	init(l,r);
}

CGaussianKernel::~CGaussianKernel()
{
	cleanup();
}

CGaussianKernel* CGaussianKernel::obtain_from_generic(CKernel* kernel)
{
	if (kernel->get_kernel_type()!=K_GAUSSIAN)
	{
		SG_SERROR("CGaussianKernel::obtain_from_generic(): provided kernel is "
				"not of type CGaussianKernel!\n");
	}

	/* since an additional reference is returned */
	SG_REF(kernel);
	return (CGaussianKernel*)kernel;
}

#include <typeinfo>
CSGObject *CGaussianKernel::shallow_copy() const
{
	// TODO: remove this after all the classes get shallow_copy properly implemented
	// this assert is to avoid any subclass of CGaussianKernel accidentally called
	// with the implement here
	ASSERT(typeid(*this) == typeid(CGaussianKernel))
	CGaussianKernel *ker = new CGaussianKernel(cache_size, get_width());
	if (lhs)
	{
		ker->init(lhs, rhs);
	}
	return ker;
}

void CGaussianKernel::cleanup()
{
	if (sq_lhs != sq_rhs)
		SG_FREE(sq_rhs);
	sq_rhs = NULL;

	SG_FREE(sq_lhs);
	sq_lhs = NULL;

	CKernel::cleanup();
}

void CGaussianKernel::precompute_squared_helper(float64_t* &buf, CDotFeatures* df)
{
	ASSERT(df)
	int32_t num_vec=df->get_num_vectors();
	buf=SG_MALLOC(float64_t, num_vec);

	for (int32_t i=0; i<num_vec; i++)
		buf[i]=df->dot(i,df, i);
}

bool CGaussianKernel::init(CFeatures* l, CFeatures* r)
{
	///free sq_{r,l}hs first
	cleanup();

	CDotKernel::init(l, r);
	precompute_squared();
	return init_normalizer();
}

float64_t CGaussianKernel::compute(int32_t idx_a, int32_t idx_b)
{
    float64_t result=distance(idx_a,idx_b);
    return CMath::exp(-result);
}

void CGaussianKernel::load_serializable_post() throw (ShogunException)
{
	CKernel::load_serializable_post();
	precompute_squared();
}

void CGaussianKernel::precompute_squared()
{
	if (!lhs || !rhs)
		return;

	precompute_squared_helper(sq_lhs, (CDotFeatures*) lhs);

	if (lhs==rhs)
		sq_rhs=sq_lhs;
	else
		precompute_squared_helper(sq_rhs, (CDotFeatures*) rhs);
}

SGMatrix<float64_t> CGaussianKernel::get_parameter_gradient(
		const TParameter* param, index_t index)
{
	REQUIRE(lhs && rhs, "Features not set!\n")

	if (!strcmp(param->m_name, "log_width"))
	{
		SGMatrix<float64_t> derivative=SGMatrix<float64_t>(num_lhs, num_rhs);

		for (int j=0; j<num_lhs; j++)
			for (int k=0; k<num_rhs; k++)
			{
				float64_t element=distance(j,k);
				derivative(j,k)=exp(-element)*element*2.0;
			}

		return derivative;
	}
	else
	{
		SG_ERROR("Can't compute derivative wrt %s parameter\n", param->m_name);
		return SGMatrix<float64_t>();
	}
}

void CGaussianKernel::init()
{
	set_width(1.0);
	sq_lhs=NULL;
	sq_rhs=NULL;
	SG_ADD(&m_log_width, "log_width", "Kernel width in log domain", MS_AVAILABLE, GRADIENT_AVAILABLE);
}

float64_t CGaussianKernel::distance(int32_t idx_a, int32_t idx_b)
{
	return (sq_lhs[idx_a]+sq_rhs[idx_b]-2*CDotKernel::compute(idx_a,idx_b))/get_width();
}
