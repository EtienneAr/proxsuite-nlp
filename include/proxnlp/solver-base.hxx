/// @file solver-base.hxx
/// Implementations for the prox solver.
#pragma once

#include "proxnlp/solver-base.hpp"

#include "proxnlp/exceptions.hpp"

#include <fmt/ostream.h>
#include <fmt/color.h>

namespace proxnlp {
template <typename Scalar>
SolverTpl<Scalar>::SolverTpl(shared_ptr<Problem> prob, const Scalar tol,
                             const Scalar mu_init, const Scalar rho_init,
                             const VerboseLevel verbose, const Scalar mu_lower,
                             const Scalar prim_alpha, const Scalar prim_beta,
                             const Scalar dual_alpha, const Scalar dual_beta,
                             const LinesearchOptions ls_options)
    : problem_(prob), merit_fun(problem_, mu_init),
      prox_penalty(prob->manifold_, manifold().neutral(),
                   rho_init *
                       MatrixXs::Identity(manifold().ndx(), manifold().ndx())),
      verbose(verbose), rho_init_(rho_init), mu_init_(mu_init),
      mu_lower_(mu_lower), bcl_params{prim_alpha, prim_beta, dual_alpha,
                                      dual_beta},
      ls_options(ls_options), target_tol(tol) {}

template <typename Scalar>
ConvergenceFlag SolverTpl<Scalar>::solve(Workspace &workspace, Results &results,
                                         const ConstVectorRef &x0,
                                         const std::vector<VectorRef> &lams0) {
  VectorXs new_lam(problem_->getTotalConstraintDim());
  new_lam.setZero();
  int nr = 0;
  const std::size_t numc = problem_->getNumConstraints();
  if (numc != lams0.size()) {
    proxnlp_runtime_error("Specified number of constraints is not the same "
                          "as the provided number of multipliers!");
  }
  for (std::size_t i = 0; i < numc; i++) {
    nr = problem_->getConstraintDim(i);
    new_lam.segment(problem_->getIndex(i), nr) = lams0[i];
  }
  return solve(workspace, results, x0, new_lam);
}

template <typename Scalar>
ConvergenceFlag SolverTpl<Scalar>::solve(Workspace &workspace, Results &results,
                                         const ConstVectorRef &x0) {
  VectorXs lams0(workspace.numdual);
  lams0.setZero();
  return solve(workspace, results, x0, lams0);
}

template <typename Scalar>
ConvergenceFlag SolverTpl<Scalar>::solve(Workspace &workspace, Results &results,
                                         const ConstVectorRef &x0,
                                         const ConstVectorRef &lams0) {
  if (verbose == 0)
    logger.active = false;

  setPenalty(mu_init_);
  setProxParameter(rho_init_);

  // init variables
  results.x_opt = x0;
  workspace.x_prev = x0;
  results.lams_opt_data = lams0;
  workspace.data_lams_prev = lams0;

  updateToleranceFailure();

  results.num_iters = 0;

  fmt::color outer_col = fmt::color::white;

  std::size_t i = 0;
  while (results.num_iters < max_iters) {
    results.mu = mu_;
    results.rho = rho_;
    if (logger.active) {
      fmt::print(fmt::emphasis::bold | fmt::fg(outer_col),
                 "[AL iter {:>2d}] omega={:.3g}, eta={:.3g}, mu={:g}\n", i,
                 inner_tol_, prim_tol_, mu_);
    }
    if (results.num_iters == 0) {
      logger.start();
    }
    solveInner(workspace, results);

    // accept new primal iterate
    workspace.x_prev = results.x_opt;
    prox_penalty.updateTarget(workspace.x_prev);

    if (results.prim_infeas < prim_tol_) {
      outer_col = fmt::color::lime_green;
      acceptMultipliers(workspace);
      if ((results.prim_infeas < target_tol) &&
          (results.dual_infeas < target_tol)) {
        // terminate algorithm
        results.converged = ConvergenceFlag::SUCCESS;
        break;
      }
      updateToleranceSuccess();
    } else {
      outer_col = fmt::color::orange_red;
      updatePenalty();
      updateToleranceFailure();
    }
    setProxParameter(rho_ * bcl_params.rho_update_factor);

    i++;
  }

  if (results.converged == SUCCESS)
    fmt::print(fmt::fg(fmt::color::dodger_blue),
               "Solver successfully converged");

  switch (results.converged) {
  case MAX_ITERS_REACHED:
    fmt::print(fmt::fg(fmt::color::orange_red),
               "Max number of iterations reached.");
    break;
  default:
    break;
  }
  fmt::print("\n");

  invokeCallbacks(workspace, results);

  return results.converged;
}

template <typename Scalar>
typename SolverTpl<Scalar>::InertiaFlag
SolverTpl<Scalar>::checkInertia(const Eigen::VectorXi &signature) const {
  const int ndx = manifold().ndx();
  const int numc = problem_->getTotalConstraintDim();
  const long n = signature.size();
  int numpos = 0;
  int numneg = 0;
  int numzer = 0;
  for (long i = 0; i < n; i++) {
    switch (signature(i)) {
    case 1:
      numpos++;
      break;
    case 0:
      numzer++;
      break;
    case -1:
      numneg++;
      break;
    default:
      proxnlp_runtime_error(
          "Matrix signature should only have Os, 1s, and -1s.");
    }
  }
  InertiaFlag flag = INERTIA_OK;
  bool pos_ok = numpos == ndx;
  bool neg_ok = numneg == numc;
  bool zer_ok = numzer == 0;
  if (!(pos_ok && neg_ok && zer_ok)) {
    if (!zer_ok)
      flag = INERTIA_HAS_ZEROS;
    else
      flag = INERTIA_BAD;
  } else {
    flag = INERTIA_OK;
  }
  return flag;
}

template <typename Scalar>
void SolverTpl<Scalar>::computeMultipliers(
    const ConstVectorRef &x, const ConstVectorRef &inner_lams_data,
    Workspace &workspace) const {
  workspace.data_shift_cstr_values =
      workspace.data_cstr_values + mu_ * workspace.data_lams_prev;
  // project multiplier estimate
  for (std::size_t i = 0; i < problem_->getNumConstraints(); i++) {
    const ConstraintSet &cstr_set = *problem_->getConstraint(i).set_;
    // apply proximal op to shifted constraint
    cstr_set.normalConeProjection(workspace.shift_cstr_values[i],
                                  workspace.lams_plus[i]);
  }
  workspace.data_lams_plus *= mu_inv_;
  workspace.data_dual_prox_err =
      mu_ * (workspace.data_lams_plus - inner_lams_data);
  workspace.data_lams_pdal = 2 * workspace.data_lams_plus - inner_lams_data;
}

/// Compute problem derivatives
template <typename Scalar>
void SolverTpl<Scalar>::computeConstraintDerivatives(const ConstVectorRef &x,
                                                     Workspace &workspace,
                                                     bool second_order) const {
  problem_->computeDerivatives(x, workspace);
  if (second_order) {
    problem_->cost().computeHessian(x, workspace.objective_hessian);
  }
  workspace.jacobians_proj_data = workspace.jacobians_data;
  for (std::size_t i = 0; i < problem_->getNumConstraints(); i++) {
    const ConstraintObject<Scalar> &cstr = problem_->getConstraint(i);
    cstr.set_->applyNormalConeProjectionJacobian(
        workspace.shift_cstr_values[i], workspace.cstr_jacobians_proj[i]);

    bool use_vhp = (use_gauss_newton && !(cstr.set_->disableGaussNewton())) ||
                   !(use_gauss_newton);
    if (second_order && use_vhp) {
      cstr.func().vectorHessianProduct(x, workspace.lams_pdal[i],
                                       workspace.cstr_vector_hessian_prod[i]);
    }
  }
}

template <typename Scalar> void SolverTpl<Scalar>::updatePenalty() {
  if (mu_ == mu_lower_) {
    setPenalty(mu_init_);
  } else {
    setPenalty(std::max(mu_ * bcl_params.mu_update_factor, mu_lower_));
  }
}

template <typename Scalar>
void SolverTpl<Scalar>::solveInner(Workspace &workspace, Results &results) {
  const int ndx = manifold().ndx();
  const long ntot = workspace.kkt_rhs.size();
  const long ndual = ntot - ndx;
  const std::size_t num_c = problem_->getNumConstraints();

  results.lams_opt_data = workspace.data_lams_prev;

  Scalar delta_last = 0.;
  Scalar delta = delta_last;
  Scalar old_delta = 0.;
  Scalar conditioning = 0;

  merit_fun.setPenalty(mu_);

  // lambda for evaluating the merit function
  auto phi_eval = [&](const Scalar alpha) {
    tryStep(manifold(), workspace, results, alpha);
    problem_->evaluate(workspace.x_trial, workspace);
    computeMultipliers(workspace.x_trial, workspace.lams_trial_data, workspace);
    return merit_fun.evaluate(workspace.x_trial, workspace.lams_trial,
                              workspace.shift_cstr_values) +
           prox_penalty.call(workspace.x_trial);
  };

  while (true) {

    //// precompute temp data

    results.value = problem_->cost().call(results.x_opt);

    problem_->evaluate(results.x_opt, workspace);
    computeMultipliers(results.x_opt, results.lams_opt_data, workspace);
    computeConstraintDerivatives(results.x_opt, workspace, true);

    results.merit = merit_fun.evaluate(results.x_opt, results.lams_opt,
                                       workspace.shift_cstr_values);
    if (rho_ > 0.) {
      results.merit += prox_penalty.call(results.x_opt);
      prox_penalty.computeGradient(results.x_opt, workspace.prox_grad);
      prox_penalty.computeHessian(results.x_opt, workspace.prox_hess);
    }

    PROXNLP_RAISE_IF_NAN_NAME(workspace.prox_grad, "prox_grad");

    //// fill in KKT RHS
    workspace.kkt_rhs.setZero();

    // add jacobian-vector products to gradients
    workspace.kkt_rhs.head(ndx) =
        workspace.objective_gradient +
        workspace.jacobians_data.transpose() * results.lams_opt_data;
    workspace.kkt_rhs.tail(ndual) = workspace.data_dual_prox_err;
    workspace.merit_gradient =
        workspace.objective_gradient +
        workspace.jacobians_data.transpose() * workspace.data_lams_pdal;

    // add proximal penalty terms
    if (rho_ > 0.) {
      workspace.kkt_rhs.head(ndx) += workspace.prox_grad;
      workspace.merit_gradient += workspace.prox_grad;
    }

    for (std::size_t i = 0; i < num_c; i++) {
      const ConstraintSet &cstr_set = *problem_->getConstraint(i).set_;
      cstr_set.computeActiveSet(workspace.cstr_values[i],
                                results.active_set[i]);
    }

    PROXNLP_RAISE_IF_NAN_NAME(workspace.kkt_rhs, "kkt_rhs");
    PROXNLP_RAISE_IF_NAN_NAME(workspace.kkt_matrix, "kkt_matrix");

    // Compute dual residual and infeasibility
    workspace.dual_residual = workspace.kkt_rhs.head(ndx);
    if (rho_ > 0.)
      workspace.dual_residual -= workspace.prox_grad;

    results.dual_infeas = math::infty_norm(workspace.dual_residual);
    for (std::size_t i = 0; i < problem_->getNumConstraints(); i++) {
      const ConstraintSet &cstr_set = *problem_->getConstraint(i).set_;

      //
      // get the "slack" Z = prox(c + mu * lam_prev)
      auto &displ_cstr = workspace.shift_cstr_values[i];
      // apply proximal operator
      cstr_set.projection(displ_cstr, displ_cstr);

      auto cstr_prox_err = workspace.cstr_values[i] - displ_cstr;
      results.constraint_violations(long(i)) = math::infty_norm(cstr_prox_err);
    }
    results.prim_infeas = math::infty_norm(results.constraint_violations);
    Scalar inner_crit = math::infty_norm(workspace.kkt_rhs);

    bool outer_cond = (results.prim_infeas <= target_tol &&
                       results.dual_infeas <= target_tol);
    if ((inner_crit <= inner_tol_) || outer_cond) {
      return;
    }

    // If not optimal: compute the step

    // fill in KKT matrix

    workspace.kkt_matrix.setZero();
    workspace.kkt_matrix.topLeftCorner(ndx, ndx) = workspace.objective_hessian;
    workspace.kkt_matrix.topRightCorner(ndx, ndual) =
        workspace.jacobians_proj_data.transpose();
    workspace.kkt_matrix.bottomLeftCorner(ndual, ndx) =
        workspace.jacobians_proj_data;
    workspace.kkt_matrix.bottomRightCorner(ndual, ndual)
        .diagonal()
        .setConstant(-mu_);
    if (rho_ > 0.) {
      workspace.kkt_matrix.topLeftCorner(ndx, ndx) += workspace.prox_hess;
    }
    for (std::size_t i = 0; i < num_c; i++) {
      const ConstraintSet &cstr_set = *problem_->getConstraint(i).set_;
      bool use_vhp = (use_gauss_newton && !cstr_set.disableGaussNewton()) ||
                     !use_gauss_newton;
      if (use_vhp) {
        workspace.kkt_matrix.topLeftCorner(ndx, ndx) +=
            workspace.cstr_vector_hessian_prod[i];
      }
    }

    // choose regularisation level

    delta = DELTA_INIT;
    InertiaFlag is_inertia_correct = INERTIA_BAD;

    while (!(is_inertia_correct == INERTIA_OK) && delta <= DELTA_MAX) {
      if (delta > 0.) {
        workspace.kkt_matrix.diagonal().head(ndx).array() += delta;
      }
      workspace.ldlt_.compute(workspace.kkt_matrix);
      conditioning = 1. / workspace.ldlt_.rcond();
      workspace.signature.array() =
          workspace.ldlt_.vectorD().array().sign().template cast<int>();
      workspace.kkt_matrix.diagonal().head(ndx).array() -= delta;
      is_inertia_correct = checkInertia(workspace.signature);
      old_delta = delta;

      if (is_inertia_correct == INERTIA_OK) {
        delta_last = delta;
        break;
      } else if (delta == 0.) {
        // check if previous was zero
        if (delta_last == 0.) {
          delta = DELTA_NONZERO_INIT; // try a set nonzero value
        } else {
          delta = std::max(DELTA_MIN, del_dec_k * delta_last);
        }
      } else {
        // check previous; decide increase factor
        if (delta_last == 0.) {
          delta *= del_inc_big;
        } else {
          delta *= del_inc_k;
        }
      }
    }

    workspace.pd_step = -workspace.kkt_rhs;
    workspace.ldlt_.solveInPlace(workspace.pd_step);

    PROXNLP_RAISE_IF_NAN_NAME(workspace.pd_step, "pd_step");

    const std::size_t MAX_REFINEMENT_STEPS = 5;
    for (std::size_t n = 0; n < MAX_REFINEMENT_STEPS; n++) {
      auto resdl = workspace.kkt_matrix * workspace.pd_step + workspace.kkt_rhs;
      Scalar resdl_norm = math::infty_norm(resdl);
      if (resdl_norm < 1e-13)
        break;
      workspace.pd_step += workspace.ldlt_.solve(-resdl);
    }

    //// Take the step

    workspace.dmerit_dir =
        workspace.merit_gradient.dot(workspace.prim_step) -
        workspace.data_dual_prox_err.dot(workspace.dual_step);

    Scalar phi0 = results.merit;
    Scalar phi_new = 0.;
    switch (ls_strat) {
    case ARMIJO: {
      phi_new = ArmijoLinesearch<Scalar>(ls_options)
                    .run(phi_eval, results.merit, workspace.dmerit_dir,
                         workspace.alpha_opt);
      break;
    }
    default:
      proxnlp_runtime_error("Unrecognized linesearch alternative.\n");
      break;
    }
    // fmt::print(" | alph_opt={:4.3e}\n", workspace.alpha_opt);

    PROXNLP_RAISE_IF_NAN_NAME(workspace.alpha_opt, "alpha_opt");
    PROXNLP_RAISE_IF_NAN_NAME(workspace.x_trial, "x_trial");
    PROXNLP_RAISE_IF_NAN_NAME(workspace.lams_trial_data, "lams_trial");
    results.x_opt = workspace.x_trial;
    results.lams_opt_data = workspace.lams_trial_data;
    results.merit = phi_new;
    PROXNLP_RAISE_IF_NAN_NAME(results.merit, "merit");

    invokeCallbacks(workspace, results);

    LogRecord record{
        results.num_iters + 1, workspace.alpha_opt, inner_crit,
        results.prim_infeas,   results.dual_infeas, delta,
        workspace.dmerit_dir,  results.merit,       phi_new - phi0};

    logger.log(record);

    results.num_iters++;
    if (results.num_iters >= max_iters) {
      results.converged = ConvergenceFlag::MAX_ITERS_REACHED;
      break;
    }
  }

  if (results.num_iters >= max_iters)
    results.converged = ConvergenceFlag::MAX_ITERS_REACHED;

  return;
}

template <typename Scalar>
void SolverTpl<Scalar>::setPenalty(const Scalar &new_mu) noexcept {
  mu_ = new_mu;
  mu_inv_ = 1. / mu_;
  merit_fun.setPenalty(mu_);
  for (std::size_t i = 0; i < problem_->getNumConstraints(); i++) {
    const ConstraintObject<Scalar> &cstr = problem_->getConstraint(i);
    cstr.set_->setProxParameters(mu_);
  }
}

template <typename Scalar>
void SolverTpl<Scalar>::setProxParameter(const Scalar &new_rho) noexcept {
  rho_ = new_rho;
  prox_penalty.weights_.setZero();
  prox_penalty.weights_.diagonal().setConstant(rho_);
}

template <typename Scalar>
void SolverTpl<Scalar>::updateToleranceFailure() noexcept {
  prim_tol_ = prim_tol0 * std::pow(mu_, bcl_params.prim_alpha);
  inner_tol_ = inner_tol0 * std::pow(mu_, bcl_params.dual_alpha);
  tolerancePostUpdate();
}

template <typename Scalar>
void SolverTpl<Scalar>::updateToleranceSuccess() noexcept {
  prim_tol_ = prim_tol_ * std::pow(mu_ / mu_upper_, bcl_params.prim_beta);
  inner_tol_ = inner_tol_ * std::pow(mu_ / mu_upper_, bcl_params.dual_beta);
  tolerancePostUpdate();
}

template <typename Scalar>
void SolverTpl<Scalar>::tolerancePostUpdate() noexcept {
  inner_tol_ = std::max(inner_tol_, inner_tol_min);
  prim_tol_ = std::max(prim_tol_, target_tol);
}

template <typename Scalar>
void SolverTpl<Scalar>::tryStep(const Manifold &manifold, Workspace &workspace,
                                const Results &results, Scalar alpha) {
  manifold.integrate(results.x_opt, alpha * workspace.prim_step,
                     workspace.x_trial);
  workspace.lams_trial_data =
      results.lams_opt_data + alpha * workspace.dual_step;
}
} // namespace proxnlp
