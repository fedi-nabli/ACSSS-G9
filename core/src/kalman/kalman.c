/* ===================================================================================================
 * core/src/kalman/kalman.c
 *
 * Kalman, see kalman.h. Loops run to k->n, never KALMAN_N_MAX: a 2-state
 * filter costs a 2-state filter.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <core/kalman/kalman.h>

#include <utils/logger/logger.h>

#include <math.h>
#include <string.h>

static bool all_finite(const double* v, uint32_t count)
{
  for (uint32_t i = 0; i < count; i++)
  {
    if (!isfinite(v[i]))
    {
      return false;
    }
  }
  return true;
}

static void load_matrix(double dst[KALMAN_N_MAX][KALMAN_N_MAX], const double* src, uint32_t n)
{
  for (uint32_t i = 0; i < n; i++)
  {
    for (uint32_t j = 0; j < n; j++)
    {
      dst[i][j] = src[i * n + j];
    }
  }
}

static void symmetrize(Kalman* k)
{
  for (uint32_t i = 0; i < k->n; i++)
  {
    for (uint32_t j = i + 1; j < k->n; j++)
    {
      const double m = 0.5 * (k->P[i][j] + k->P[j][i]);
      k->P[i][j] = m;
      k->P[j][i] = m;
    }
  }
}

bool kalman_init(Kalman* k, uint32_t n, const double* F, const double* H, const double* Q, double r, const double* x0,
                 const double* P0)
{
  if (n == 0 || n > KALMAN_N_MAX)
  {
    MTS_LOG_ERROR("kalman: %u states, must be 1 .. %d", n, KALMAN_N_MAX);
    return false;
  }
  if (!(r >= 0.0) || !isfinite(r))
  {
    MTS_LOG_ERROR("kalman: measurement noise r = %g, must be finite and >= 0", r);
    return false;
  }
  if (!all_finite(F, n * n) || !all_finite(H, n) || !all_finite(Q, n * n) || !all_finite(x0, n)
      || !all_finite(P0, n * n))
  {
    MTS_LOG_ERROR("kalman: a matrix or vector holds a value that is not finite");
    return false;
  }

  memset(k, 0, sizeof(*k));
  k->n = n;
  load_matrix(k->F, F, n);
  load_matrix(k->Q, Q, n);
  for (uint32_t i = 0; i < n; i++)
  {
    k->H[i] = H[i];
  }
  k->r = r;
  kalman_reset(k, x0, P0);
  return true;
}

void kalman_reset(Kalman* k, const double* x0, const double* P0)
{
  for (uint32_t i = 0; i < k->n; i++)
  {
    k->x[i] = x0[i];
  }
  load_matrix(k->P, P0, k->n);
  symmetrize(k);
  k->innovation = 0.0;
  k->innovation_var = 0.0;
}

void kalman_predict(Kalman* k)
{
  const uint32_t n = k->n;

  // x = F x
  double x[KALMAN_N_MAX];
  for (uint32_t i = 0; i < n; i++)
  {
    double s = 0.0;
    for (uint32_t j = 0; j < n; j++)
    {
      s += k->F[i][j] * k->x[j];
    }
    x[i] = s;
  }
  memcpy(k->x, x, n * sizeof(double));

  // P = F P F' + Q, as (F P) F'
  double FP[KALMAN_N_MAX][KALMAN_N_MAX];
  for (uint32_t i = 0; i < n; i++)
  {
    for (uint32_t j = 0; j < n; j++)
    {
      double s = 0.0;
      for (uint32_t m = 0; m < n; m++)
      {
        s += k->F[i][m] * k->P[m][j];
      }
      FP[i][j] = s;
    }
  }
  for (uint32_t i = 0; i < n; i++)
  {
    for (uint32_t j = 0; j < n; j++)
    {
      double s = k->Q[i][j];
      for (uint32_t m = 0; m < n; m++)
      {
        s += FP[i][m] * k->F[j][m];
      }
      k->P[i][j] = s;
    }
  }
  symmetrize(k);
}

bool kalman_update(Kalman* k, double z)
{
  const uint32_t n = k->n;
  if (!isfinite(z))
  {
    return false;
  }

  // PH = P H' (a column), S = H P H' + r
  double PH[KALMAN_N_MAX];
  double S = k->r;
  double hx = 0.0;
  for (uint32_t i = 0; i < n; i++)
  {
    double s = 0.0;
    for (uint32_t j = 0; j < n; j++)
    {
      s += k->P[i][j] * k->H[j];
    }
    PH[i] = s;
    S += k->H[i] * s;
    hx += k->H[i] * k->x[i];
  }
  if (!(S > 0.0) || !isfinite(S))
  {
    return false; // nothing observable, or a broken covariance: keep what we have
  }

  const double y = z - hx;
  double K[KALMAN_N_MAX];
  for (uint32_t i = 0; i < n; i++)
  {
    K[i] = PH[i] / S;
    k->x[i] += K[i] * y;
  }

  // Joseph form: A = I - K H, P = A P A' + r K K'
  double A[KALMAN_N_MAX][KALMAN_N_MAX];
  for (uint32_t i = 0; i < n; i++)
  {
    for (uint32_t j = 0; j < n; j++)
    {
      A[i][j] = (i == j ? 1.0 : 0.0) - K[i] * k->H[j];
    }
  }
  double AP[KALMAN_N_MAX][KALMAN_N_MAX];
  for (uint32_t i = 0; i < n; i++)
  {
    for (uint32_t j = 0; j < n; j++)
    {
      double s = 0.0;
      for (uint32_t m = 0; m < n; m++)
      {
        s += A[i][m] * k->P[m][j];
      }
      AP[i][j] = s;
    }
  }
  for (uint32_t i = 0; i < n; i++)
  {
    for (uint32_t j = 0; j < n; j++)
    {
      double s = k->r * K[i] * K[j];
      for (uint32_t m = 0; m < n; m++)
      {
        s += AP[i][m] * A[j][m];
      }
      k->P[i][j] = s;
    }
  }
  symmetrize(k);

  k->innovation = y;
  k->innovation_var = S;
  return true;
}

void kalman_project(const Kalman* k, uint32_t steps, double* out)
{
  const uint32_t n = k->n;
  double x[KALMAN_N_MAX];
  double next[KALMAN_N_MAX];
  memcpy(x, k->x, n * sizeof(double));

  for (uint32_t step = 0; step < steps; step++)
  {
    for (uint32_t i = 0; i < n; i++)
    {
      double s = 0.0;
      for (uint32_t j = 0; j < n; j++)
      {
        s += k->F[i][j] * x[j];
      }
      next[i] = s;
    }
    memcpy(x, next, n * sizeof(double));

    double z = 0.0;
    for (uint32_t i = 0; i < n; i++)
    {
      z += k->H[i] * x[i];
    }
    out[step] = z;
  }
}

void kalman_set_F(Kalman* k, const double* F)
{
  load_matrix(k->F, F, k->n);
}

void kalman_set_Q(Kalman* k, const double* Q)
{
  load_matrix(k->Q, Q, k->n);
}

void kalman_set_r(Kalman* k, double r)
{
  k->r = r;
}
