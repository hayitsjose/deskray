#pragma once
#include <cmath>

// One Euro filter (Casiez et al., CHI 2012): heavy smoothing when the signal is
// slow (kills controller jitter on small targets), little when it moves fast
// (no lag on big sweeps). https://gery.casiez.net/1euro/
class OneEuroFilter {
 public:
  OneEuroFilter(double minCutoffHz, double beta, double dCutoffHz = 1.0)
      : minCutoff_(minCutoffHz), beta_(beta), dCutoff_(dCutoffHz) {}

  double filter(double x, double dtSec) {
    if (!init_ || dtSec <= 0.0) {
      init_ = true;
      x_ = x;
      dx_ = 0.0;
      return x;
    }
    const double dx = (x - x_) / dtSec;
    dx_ += alpha(dCutoff_, dtSec) * (dx - dx_);
    const double cutoff = minCutoff_ + beta_ * std::fabs(dx_);
    x_ += alpha(cutoff, dtSec) * (x - x_);
    return x_;
  }

  void reset() { init_ = false; }

 private:
  static double alpha(double cutoffHz, double dtSec) {
    const double tau = 1.0 / (2.0 * M_PI * cutoffHz);
    return 1.0 / (1.0 + tau / dtSec);
  }

  double minCutoff_, beta_, dCutoff_;
  bool init_ = false;
  double x_ = 0.0, dx_ = 0.0;
};
