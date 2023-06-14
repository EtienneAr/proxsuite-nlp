#include "proxnlp/python/fwd.hpp"
#include "proxnlp/python/policies.hpp"

#include "proxnlp/workspace.hpp"

namespace proxnlp {
namespace python {
void exposeWorkspace() {
  using context::Scalar;
  using context::Workspace;
  bp::class_<Workspace, boost::noncopyable>(
      "Workspace", "SolverTpl workspace.",
      bp::init<const context::Problem &>(bp::args("self", "problem")))
      .def_readonly("kkt_matrix", &Workspace::kkt_matrix, "KKT matrix buffer.")
      .def_readonly("kkt_rhs", &Workspace::kkt_rhs,
                    "KKT system right-hand side buffer.")
      .add_property("prim_step", bp::make_getter(&Workspace::prim_step,
                                                 policies::return_by_value))
      .add_property("dual_step", bp::make_getter(&Workspace::dual_step,
                                                 policies::return_by_value))
      .def_readonly("objective_value", &Workspace::objective_value)
      .def_readonly("objective_gradient", &Workspace::objective_gradient)
      .def_readonly("objective_hessian", &Workspace::objective_hessian)
      .def_readonly("merit_gradient", &Workspace::merit_gradient)
      .def_readonly("data_cstr_values", &Workspace::data_cstr_values)
      .def_readonly("cstr_values", &Workspace::cstr_values,
                    "Vector constraint residuals.")
      .def_readonly("data_shift_cstr_values",
                    &Workspace::data_shift_cstr_values,
                    "Shifted constraint values.")
      .def_readonly("shift_cstr_proj", &Workspace::shift_cstr_proj,
                    "Projected shifted constraint residuals.")
      .def_readonly("dual_residuals", &Workspace::dual_residual,
                    "Dual vector residual.")
      .def_readonly("data_jacobians", &Workspace::data_jacobians,
                    "Constraint Jacobians.")
      .def_readonly("data_hessians", &Workspace::data_hessians,
                    "Constraint vector-Hessian product matrices.")
      .def_readonly("cstr_jacobians", &Workspace::cstr_jacobians,
                    "Block jacobians.")
      .def_readonly("data_jacobians_proj", &Workspace::data_jacobians_proj,
                    "Projected constraint Jacobians.")
      .def_readonly("lams_plus", &Workspace::lams_plus,
                    "First-order multiplier estimates.")
      .def_readonly("lams_plus_reproj", &Workspace::lams_plus_reproj,
                    "Product of projection Jacobian and first-order multiplier "
                    "estimates.")
      .def_readonly("lams_pdal", &Workspace::lams_pdal,
                    "Primal-dual multiplier estimates.")
      .def_readonly("alpha_opt", &Workspace::alpha_opt,
                    "Computed linesearch step length.")
      .def_readonly("dmerit_dir", &Workspace::dmerit_dir)
      .def(
          "ldlt",
          +[](const Workspace &ws) -> const linalg::ldlt_base<Scalar> & {
            return *ws.ldlt_;
          },
          policies::return_internal_reference,
          "Returns a reference to the underlying LDLT solver.");

  using LDLTBase = linalg::ldlt_base<Scalar>;
  bp::class_<LDLTBase, boost::noncopyable>(
      "LDLTBase", "Base class for LDLT solvers.", bp::no_init)
      .def("compute", &LDLTBase::compute, policies::return_internal_reference,
           bp::args("self", "mat"))
      .def("solveInPlace", &LDLTBase::solveInPlace, bp::args("self", "rhsAndX"))
      .def("matrixLDLT", &LDLTBase::matrixLDLT, policies::return_by_value,
           "Get the current value of the decomposition matrix. This makes a "
           "copy.");
  bp::class_<linalg::DenseLDLT<Scalar>, bp::bases<LDLTBase>>("DenseLDLT",
                                                             bp::no_init);
  using BlockLDLT = linalg::BlockLDLT<Scalar>;
  bp::class_<BlockLDLT, bp::bases<LDLTBase>>("BlockLDLT", bp::no_init)
      .def("print_sparsity", &BlockLDLT::print_sparsity, bp::args("self"),
           "Print the sparsity pattern of the matrix to factorize.");
#ifdef PROXNLP_ENABLE_PROXSUITE_LDLT
  bp::class_<linalg::ProxSuiteLDLTWrapper<Scalar>, bp::bases<LDLTBase>>(
      "ProxSuiteLDLT", "Wrapper around ProxSuite's custom LDLT.", bp::no_init);
#endif
}

} // namespace python
} // namespace proxnlp
