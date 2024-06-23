template<typename Real, int n>
inline void TridiagSym(Real V[n*n], Real d[n], Real e[n])
{
	for (int j = 0; j < n; ++j)
		d[j] = V[n * (n - 1) + j];

	for (int i = n - 1; i > 0; --i)
	{
		Real scale = 0.0;
		Real h = 0.0;
		for (int k = 0; k < i; ++k)
			scale = scale + abs<Real>(d[k]);

		if (scale == 0.0)
		{
			e[i] = d[i - 1];
			for (int j = 0; j < i; ++j)
			{
				d[j] = V[n * (i - 1) + j];
				V[n * i + j] = 0.0;
				V[n * j + i] = 0.0;
			}
		}
		else
		{
			for (int k = 0; k < i; ++k)
			{
				d[k] /= scale;
				h += d[k] * d[k];
			}
			Real f = d[i - 1];
			Real g = sqrt(h);
			if (f > 0) g = -g;
			e[i] = scale * g;
			h = h - f * g;
			d[i - 1] = f - g;
			for (int j = 0; j < i; ++j)
				e[j] = 0.0;
			for (int j = 0; j < i; ++j)
			{
				f = d[j];
				V[n * j + i] = f;
				g = e[j] + V[n * j + j] * f;
				for (int k = j + 1; k <= i - 1; ++k)
				{
					g += V[n * k + j] * d[k];
					e[k] += V[n * k + j] * f;
				}
				e[j] = g;
			}
			f = 0.0;
			for (int j = 0; j < i; ++j)
			{
				e[j] /= h;
				f += e[j] * d[j];
			}
			Real hh = f / (h + h);
			for (int j = 0; j < i; ++j)
				e[j] -= hh * d[j];
			for (int j = 0; j < i; ++j)
			{
				f = d[j];
				g = e[j];
				for (int k = j; k <= i - 1; ++k)
					V[n * k + j] -= (f * e[k] + g * d[k]);
				d[j] = V[n * (i - 1) + j];
				V[n * i + j] = 0.0;
			}
		}
		d[i] = h;
	}
	for (int i = 0; i < n - 1; ++i)
	{
		V[n * (n - 1) + i] = V[n * i + i];
		V[n * i + i] = 1.0;
		Real h = d[i + 1];
		if (h != 0.0)
		{
			for (int k = 0; k <= i; ++k)
				d[k] = V[n * k + i + 1] / h;
			for (int j = 0; j <= i; ++j)
			{
				Real g = 0.0;
				for (int k = 0; k <= i; ++k)
					g += V[n * k + i + 1] * V[n * k + j];
				for (int k = 0; k <= i; ++k)
					V[n * k + j] -= g * d[k];
			}
		}
		for (int k = 0; k <= i; ++k)
			V[n * k + i + 1] = 0.0;
	}

	for (int j = 0; j < n; ++j)
	{
		d[j] = V[n * (n - 1) + j];
		V[n * (n - 1) + j] = 0.0;
	}
	V[n * (n - 1) + n - 1] = 1.0;
	e[0] = 0.0;
}

template<typename Real, int n>
inline void TridiagQLSym(Real V[n*n], Real d[n], Real e[n])
{
	for (int i = 1; i < n; ++i)
		e[i - 1] = e[i];
	e[n - 1] = 0.0;
	Real f = 0.0;
	Real tst1 = 0.0;
	Real eps;
	if (sizeof(Real) == sizeof(double))
	{
		eps = pow(2.0, -52.0);
	}
	else
	{
		eps = pow(2.0, -23.0);
	}
	for (int l = 0; l < n; ++l)
	{
		tst1 = max(tst1, abs(d[l]) + abs(e[l]));
		int m = l;
		while (m < n)
		{
			if (abs(e[m]) <= eps * tst1) break;
			++m;
		}
		if (m > l)
		{
			int iter = 0;
			do
			{
				iter = iter + 1;
				Real g = d[l];
				Real p = (d[l + 1] - g) / (2.0 * e[l]);
				Real r = sqrt(p * p + 1.0);
				if (p < 0) r = -r;
				d[l] = e[l] / (p + r);
				d[l + 1] = e[l] * (p + r);
				Real dl1 = d[l + 1];
				Real h = g - d[l];
				for (int i = l + 2; i < n; ++i)
					d[i] -= h;
				f = f + h;

				p = d[m];
				Real c = 1.0;
				Real c2 = c;
				Real c3 = c;
				Real el1 = e[l + 1];
				Real s = 0.0;
				Real s2 = 0.0;
				for (int i = m - 1; i >= l; --i)
				{
					c3 = c2;
					c2 = c;
					s2 = s;
					g = c * e[i];
					h = c * p;
					r = sqrt(p * p + e[i] * e[i]);
					e[i + 1] = s * r;
					s = e[i] / r;
					c = p / r;
					p = c * d[i] - s * g;
					d[i + 1] = h + s * (c * g + s * d[i]);

					for (int k = 0; k < n; ++k)
					{
						h = V[n * k + i + 1];
						V[n * k + i + 1] = s * V[n * k + i] + c * h;
						V[n * k + i] = c * V[n * k + i] - s * h;
					}
				}
				p = -s * s2 * c3 * el1 * e[l] / dl1;
				e[l] = s * p;
				d[l] = c * p;
			} while (abs(e[l]) > eps * tst1);
		}
		d[l] = d[l] + f;
		e[l] = 0.0;
	}

	for (int i = 0; i < n - 1; ++i)
	{
		int k = i;
		Real p = d[i];
		for (int j = i + 1; j < n; ++j)
		{
			if (d[j] < p)
			{
				k = j;
				p = d[j];
			}
		}
		if (k != i)
		{
			d[k] = d[i];
			d[i] = p;
			for (int j = 0; j < n; ++j)
			{
				p = V[n * j + i];
				V[n * j + i] = V[n * j + k];
				V[n * j + k] = p;
			}
		}
	}
}

template<typename Real, unsigned int N>
void __EigenDecompositionSym(Real A[N*N], Real V[N*N], Real d[N])
{
	Real e[N];
	for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j)
		V[N * i + j] = A[N * i + j];
	TridiagSym<Real>(N, V, d, e);
	TridiagQLSym<Real>(N, V, d, e);
}

template<typename Real, unsigned int N>
__host__ __device__ __forceinline__
void EigenDecompositionSym(Real a[N*N], Real eig_val[N], Real eig_vec[N*N])
{
	const unsigned int N2 = N * N;
	Real A[N2];
	for (int i = 0; i < N2; ++i) A[i] = a[i];
	Real V[N2];
	Real d[N];
	__EigenDecompositionSym<Real, N>(A, V, d);
	for (int i = 0; i < N; ++i) eig_val[i] = d[N - i - 1];
	for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j)
		eig_vec[N * i + j] = V[N * i + N - j - 1];
}

template<typename Real, unsigned int N>
inline void symm_semipositive_projection(Real mat_symm[N*N])
{
	Real eig_val[N], eig_vec[N * N];
	EigenDecompositionSym<Real, N>(mat_symm, eig_val, eig_vec);
	for (int i = 0; i < N; ++i) eig_val[i] = eig_val[i] > 0.0 ? eig_val[i] : 0.0;
	Real tmp[N * N];
	for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j)
		tmp[N * i + j] = eig_vec[N * i + j] * eig_val[j];
	memset(mat_symm, 0, sizeof(Real) * N * N);
	for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) for (int k = 0; k < N; ++k)
		mat_symm[N * i + j] += tmp[i * N + k] * eig_vec[j * N + k];
}

template<typename Real>
inline void symm_semipositive_projection5(Real mat_symm[25])
{
	symm_semipositive_projection<Real, 5>(mat_symm);
}

template <class Real, int rows, int cols>
inline void mv(Real X[rows * cols], const Real A[rows * cols], const Real b[cols])
{
    memset(X, 0, sizeof(Real) * rows * cols);
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            X[i] += A[i * cols + j] * b[j];
}

template<typename Real>
inline void matrix_inverse4(Real mat, Real mat_inv[25])
{
    Real t2 = mat[0]*mat[5]*mat[10]*mat[15];
    Real t3 = mat[0]*mat[5]*mat[11]*mat[14];
    Real t4 = mat[0]*mat[6]*mat[9]*mat[15];
    Real t5 = mat[0]*mat[6]*mat[11]*mat[13];
    Real t6 = mat[0]*mat[7]*mat[9]*mat[14];
    Real t7 = mat[0]*mat[7]*mat[10]*mat[13];
    Real t8 = mat[1]*mat[4]*mat[10]*mat[15];
    Real t9 = mat[1]*mat[4]*mat[11]*mat[14];
    Real t10 = mat[1]*mat[6]*mat[8]*mat[15];
    Real t11 = mat[1]*mat[6]*mat[11]*mat[12];
    Real t12 = mat[1]*mat[7]*mat[8]*mat[14];
    Real t13 = mat[1]*mat[7]*mat[10]*mat[12];
    Real t14 = mat[2]*mat[4]*mat[9]*mat[15];
    Real t15 = mat[2]*mat[4]*mat[11]*mat[13];
    Real t16 = mat[2]*mat[5]*mat[8]*mat[15];
    Real t17 = mat[2]*mat[5]*mat[11]*mat[12];
    Real t18 = mat[2]*mat[7]*mat[8]*mat[13];
    Real t19 = mat[2]*mat[7]*mat[9]*mat[12];
    Real t20 = mat[3]*mat[4]*mat[9]*mat[14];
    Real t21 = mat[3]*mat[4]*mat[10]*mat[13];
    Real t22 = mat[3]*mat[5]*mat[8]*mat[14];
    Real t23 = mat[3]*mat[5]*mat[10]*mat[12];
    Real t24 = mat[3]*mat[6]*mat[8]*mat[13];
    Real t25 = mat[3]*mat[6]*mat[9]*mat[12];
    Real t26 = -t3;
    Real t27 = -t4;
    Real t28 = -t7;
    Real t29 = -t8;
    Real t30 = -t11;
    Real t31 = -t12;
    Real t32 = -t15;
    Real t33 = -t16;
    Real t34 = -t19;
    Real t35 = -t20;
    Real t36 = -t23;
    Real t37 = -t24;
    Real t38 = t2+t5+t6+t9+t10+t13+t14+t17+t18+t21+t22+t25+t26+t27+t28+t29+t30+t31+t32+t33+t34+t35+t36+t37;
    Real t39 = 1.0/t38;
    mat_inv[0]  = t39*(mat[5]*mat[10]*mat[15]-mat[5]*mat[11]*mat[14]-mat[6]*mat[9]*mat[15]+mat[6]*mat[11]*mat[13]+mat[7]*mat[9]*mat[14]-mat[7]*mat[10]*mat[13]);
    mat_inv[1]  = -t39*(mat[1]*mat[10]*mat[15]-mat[1]*mat[11]*mat[14]-mat[2]*mat[9]*mat[15]+mat[2]*mat[11]*mat[13]+mat[3]*mat[9]*mat[14]-mat[3]*mat[10]*mat[13]);
    mat_inv[2]  = t39*(mat[1]*mat[6]*mat[15]-mat[1]*mat[7]*mat[14]-mat[2]*mat[5]*mat[15]+mat[2]*mat[7]*mat[13]+mat[3]*mat[5]*mat[14]-mat[3]*mat[6]*mat[13]);
    mat_inv[3]  = -t39*(mat[1]*mat[6]*mat[11]-mat[1]*mat[7]*mat[10]-mat[2]*mat[5]*mat[11]+mat[2]*mat[7]*mat[9]+mat[3]*mat[5]*mat[10]-mat[3]*mat[6]*mat[9]);
    mat_inv[4]  = -t39*(mat[4]*mat[10]*mat[15]-mat[4]*mat[11]*mat[14]-mat[6]*mat[8]*mat[15]+mat[6]*mat[11]*mat[12]+mat[7]*mat[8]*mat[14]-mat[7]*mat[10]*mat[12]);
    mat_inv[5]  = t39*(mat[0]*mat[10]*mat[15]-mat[0]*mat[11]*mat[14]-mat[2]*mat[8]*mat[15]+mat[2]*mat[11]*mat[12]+mat[3]*mat[8]*mat[14]-mat[3]*mat[10]*mat[12]);
    mat_inv[6]  = -t39*(mat[0]*mat[6]*mat[15]-mat[0]*mat[7]*mat[14]-mat[2]*mat[4]*mat[15]+mat[2]*mat[7]*mat[12]+mat[3]*mat[4]*mat[14]-mat[3]*mat[6]*mat[12]);
    mat_inv[7]  = t39*(mat[0]*mat[6]*mat[11]-mat[0]*mat[7]*mat[10]-mat[2]*mat[4]*mat[11]+mat[2]*mat[7]*mat[8]+mat[3]*mat[4]*mat[10]-mat[3]*mat[6]*mat[8]);
    mat_inv[8]  = t39*(mat[4]*mat[9]*mat[15]-mat[4]*mat[11]*mat[13]-mat[5]*mat[8]*mat[15]+mat[5]*mat[11]*mat[12]+mat[7]*mat[8]*mat[13]-mat[7]*mat[9]*mat[12]);
    mat_inv[9]  = -t39*(mat[0]*mat[9]*mat[15]-mat[0]*mat[11]*mat[13]-mat[1]*mat[8]*mat[15]+mat[1]*mat[11]*mat[12]+mat[3]*mat[8]*mat[13]-mat[3]*mat[9]*mat[12]);
    mat_inv[10] = t39*(mat[0]*mat[5]*mat[15]-mat[0]*mat[7]*mat[13]-mat[1]*mat[4]*mat[15]+mat[1]*mat[7]*mat[12]+mat[3]*mat[4]*mat[13]-mat[3]*mat[5]*mat[12]);
    mat_inv[11] = -t39*(mat[0]*mat[5]*mat[11]-mat[0]*mat[7]*mat[9]-mat[1]*mat[4]*mat[11]+mat[1]*mat[7]*mat[8]+mat[3]*mat[4]*mat[9]-mat[3]*mat[5]*mat[8]);
    mat_inv[12] = -t39*(mat[4]*mat[9]*mat[14]-mat[4]*mat[10]*mat[13]-mat[5]*mat[8]*mat[14]+mat[5]*mat[10]*mat[12]+mat[6]*mat[8]*mat[13]-mat[6]*mat[9]*mat[12]);
    mat_inv[13] = t39*(mat[0]*mat[9]*mat[14]-mat[0]*mat[10]*mat[13]-mat[1]*mat[8]*mat[14]+mat[1]*mat[10]*mat[12]+mat[2]*mat[8]*mat[13]-mat[2]*mat[9]*mat[12]);
    mat_inv[14] = -t39*(mat[0]*mat[5]*mat[14]-mat[0]*mat[6]*mat[13]-mat[1]*mat[4]*mat[14]+mat[1]*mat[6]*mat[12]+mat[2]*mat[4]*mat[13]-mat[2]*mat[5]*mat[12]);
    mat_inv[15] = t39*(mat[0]*mat[5]*mat[10]-mat[0]*mat[6]*mat[9]-mat[1]*mat[4]*mat[10]+mat[1]*mat[6]*mat[8]+mat[2]*mat[4]*mat[9]-mat[2]*mat[5]*mat[8]);
}

template<typename Real>
inline Real compute_energy(unsigned int nConstraints, Real c[3], Real v[3], Real a, Real lambda, Real n[3])
{
    return pow(c-a*exp(lambda*(n[0]*v[0]+n[1]*v[1]+n[2]*v[2]-1.0)),2.0);
}

template<typename Real>
inline void compute_gradient(unsigned int nConstraints, Real c[3], Real v[3], Real grad[7], Real a, Real lambda, Real n[3])
{
    Real t2 = n[0]*v[0];
    Real t3 = n[1]*v[1];
    Real t4 = n[2]*v[2];
    Real t5 = t2+t3+t4-1.0;
    Real t6 = lambda*t5;
    Real t7 = exp(t6);
    Real t8 = a*t7;
    Real t9 = -t8;
    Real t10 = c+t9;
    grad[0] = t7*t10*-2.0;
    grad[1] = t5*t8*t10*-2.0;
    grad[2] = lambda*t8*t10*v[0]*-2.0;
    grad[3] = lambda*t8*t10*v[1]*-2.0;
    grad[4] = lambda*t8*t10*v[2]*-2.0;
}

template<typename Real>
inline void compute_hessian(Real c[3], Real v[3], Real hess[49], Real a, Real lambda, Real n[3])
{
    Real t2 = n[0]*v[0];
    Real t3 = n[1]*v[1];
    Real t4 = n[2]*v[2];
    Real t5 = a*a;
    Real t6 = lambda*lambda;
    Real t7 = v[0]*v[0];
    Real t8 = v[1]*v[1];
    Real t9 = v[2]*v[2];
    Real t10 = t2+t3+t4-1.0;
    Real t11 = t10*t10;
    Real t12 = lambda*t10;
    Real t13 = exp(t12);
    Real t14 = t12*2.0;
    Real t15 = exp(t14);
    Real t16 = a*t13;
    Real t17 = -t16;
    Real t18 = a*lambda*t15*v[0]*2.0;
    Real t19 = a*lambda*t15*v[1]*2.0;
    Real t20 = a*lambda*t15*v[2]*2.0;
    Real t22 = t5*t6*t15*v[0]*v[1]*2.0;
    Real t23 = t5*t6*t15*v[0]*v[2]*2.0;
    Real t24 = t5*t6*t15*v[1]*v[2]*2.0;
    Real t25 = a*t10*t15*2.0;
    Real t26 = t5*t14*t15*v[0];
    Real t27 = t5*t14*t15*v[1];
    Real t28 = t5*t14*t15*v[2];
    Real t21 = c+t17;
    Real t29 = t16*t21*v[0]*2.0;
    Real t30 = t16*t21*v[1]*2.0;
    Real t31 = t16*t21*v[2]*2.0;
    Real t32 = lambda*t13*t21*v[0]*2.0;
    Real t33 = lambda*t13*t21*v[1]*2.0;
    Real t34 = lambda*t13*t21*v[2]*2.0;
    Real t44 = t6*t16*t21*v[0]*v[1]*-2.0;
    Real t45 = t6*t16*t21*v[0]*v[2]*-2.0;
    Real t46 = t6*t16*t21*v[1]*v[2]*-2.0;
    Real t47 = t10*t13*t21*2.0;
    Real t52 = t12*t16*t21*v[0]*-2.0;
    Real t53 = t12*t16*t21*v[1]*-2.0;
    Real t54 = t12*t16*t21*v[2]*-2.0;
    Real t35 = -t29;
    Real t36 = -t30;
    Real t37 = -t31;
    Real t38 = -t32;
    Real t39 = -t33;
    Real t40 = -t34;
    Real t48 = -t47;
    Real t58 = t22+t44;
    Real t59 = t23+t45;
    Real t60 = t24+t46;
    Real t55 = t18+t38;
    Real t56 = t19+t39;
    Real t57 = t20+t40;
    Real t61 = t25+t48;
    Real t62 = t26+t35+t52;
    Real t63 = t27+t36+t53;
    Real t64 = t28+t37+t54;
    hess[0] = t15*2.0;
    hess[1] = t61;
    hess[2] = t55;
    hess[3] = t56;
    hess[4] = t57;
    hess[5] = t61;
    hess[6] = t5*t11*t15*2.0-t11*t16*t21*2.0;
    hess[7] = t62;
    hess[8] = t63;
    hess[9] = t64;
    hess[10] = t55;
    hess[11] = t62;
    hess[12] = t5*t6*t7*t15*2.0-t6*t7*t16*t21*2.0;
    hess[13] = t58;
    hess[14] = t59;
    hess[15] = t56;
    hess[16] = t63;
    hess[17] = t58;
    hess[18] = t5*t6*t8*t15*2.0-t6*t8*t16*t21*2.0;
    hess[19] = t60;
    hess[20] = t57;
    hess[21] = t64;
    hess[22] = t59;
    hess[23] = t60;
    hess[24] = t5*t6*t9*t15*2.0-t6*t9*t16*t21*2.0;
}

template<typename Real>
inline Real safe_step(unsigned int nConstraints, Real c, Real v[3], Real x[3], Real d)
{
    Real e0 = 0.;
    for(unsigned int i = 0; i < nConstraints; ++i)
    {
        e0 += compute_energy<Real>(nConstraints, c, &v[3*i], x[0], x[1], &x[2]);
    }
    Real alpha = 1.;
    while(true)
    {
        Real x_tmp[5]; for(unsigned int i=0; i<5; ++i) x_tmp[i] = x[i] + alpha * d[i];
        Real e = 0.;
        for(unsigned int i = 0; i < nConstraints; ++i)
        {
            e += compute_energy<Real>(nConstraints, c, &v[3*i], x[0], x[1], &x[2]);
        }
        if (e >= e0)
        {
            alpha *= 0.5;
        }
        else
        {
            break;
        }
    }
    for(unsigned int i=0; i<5; ++i) x[i] = x_tmp[i];
}

template<typename Real>
inline void newton_exp(unsigned int nConstraints, Real c[3], Real v[3], inout Real a[3], inout Real lambda, inout Real n[3])
{
    Real d[5];
    Real g[5];
    Real H[25];
    Real H_inv[25];
    Real x[5] = {a, lambda, n[0], n[1], n[2]};
    while(true)
    {
        memset(g, 0, sizeof(Real) * 5);
        memset(H, 0, sizeof(Real) * 25);
        for(unsigned int i = 0; i < nConstraints; ++i)
        {
            compute_gradient<Real>(nConstraints, c[i], &v[3*i], g, a, lambda, n);
            compute_hessian<Real>(nConstraints, c[i], &v[3*i], H, a, lambda, n);
        }
        matrix_inverse4<Real>(H, H_inv);
        mv<Real>(d, H_inv, g, 5, 5);
        safe_step<Real>(nConstraints, c, v, x, d);
        if(dot<Real>(d,d)<1e-6)
        {
            break;
        }
    }
    a = x[0]; lambda = x[1]; n[0] = x[2]; n[1] = x[3]; n[2] = x[4];
}