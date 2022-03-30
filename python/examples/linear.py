import numpy as np
import lienlp
from lienlp.residuals import LinearResidual
from lienlp.costs import QuadDistanceCost
from lienlp.manifolds import EuclideanSpace
from lienlp.constraints import EqualityConstraint, NegativeOrthant

nx = 2
np.random.seed(42)
space = EuclideanSpace(nx)
nres = 1
A = np.random.randn(nres, nx)
b = np.random.randn(nres)
x0 = np.linalg.lstsq(A, -b)[0]
x1 = np.random.randn(nx) * 3

resdl = LinearResidual(A, b)

print("x0:", x0, "resdl(x0):", resdl(x0))
print("x1:", x1, "resdl(x1):", resdl(x1))
assert np.allclose(resdl.computeJacobian(x0), A)
assert np.allclose(resdl(x0), 0.)
assert np.allclose(resdl(np.zeros_like(x0)), b)

print("Residual nx :", resdl.nx)
print("Residual ndx:", resdl.ndx)
print("Residual nr :", resdl.nr)


cstr1 = EqualityConstraint(resdl)
cstr2 = NegativeOrthant(resdl)

print(cstr1.projection(x0), "should be zero")
print(cstr1.normalConeProjection(x0), "should be x0")

print("proj  x0:", cstr2.projection(x0))
print("dproj x0:", cstr2.normalConeProjection(x0))
print("proj  x1:", cstr2.projection(x1))
print("dproj x1:", cstr2.normalConeProjection(x1))

# DEFINE A PROBLEM AND SOLVE IT
x_target = np.random.randn(nx) * 10
cost_ = QuadDistanceCost(space, x_target, np.eye(nx))
problem = lienlp.Problem(cost_)
print("Problem:", problem)
print("Target :", x_target)
problem = lienlp.Problem(cost_, [cstr1])
# problem = lienlp.Problem(cost_, [cstr2])


results = lienlp.Results(nx, problem)
workspace = lienlp.Workspace(nx, nx, problem)


class DumbCallback(lienlp.BaseCallback):

    def __init__(self):
        pass

    def call(self):
        print("Calling dumb callback!")


cb = lienlp.HistoryCallback(workspace, results)
cb2 = DumbCallback()

solver = lienlp.Solver(space, problem)
solver.register_callback(cb)
# solver.register_callback(cb2)
x_init = np.random.randn(nx) * 10
lams0 = [np.random.randn(resdl.nr)]
solver.solve(workspace, results, x_init, lams0)

print(" values:\n", cb.storage.values.tolist())

import matplotlib.pyplot as plt

plt.rcParams['lines.linewidth'] = 1.

xs_ = np.stack(cb.storage.xs.tolist())

plt.subplot(121)
plt.plot(*xs_.T, ls='--', marker='.', markersize=5)
for i, x in enumerate(xs_):
    plt.annotate("$x_{{{}}}$".format(i), x, color='b',
                 xytext=(10, 10), textcoords='offset pixels')
plt.scatter(*x_target, label='target $\\bar{x}$', facecolor=(.8,0,0,.5),
            edgecolors='k', zorder=2)
plt.legend()
plt.title("Trajectory of optimization")

plt.subplot(122)
values_ = cb.storage.values.tolist()
plt.plot(range(1, len(values_) + 1), cb.storage.values.tolist())
plt.xlabel("Iterate")
plt.yscale("log")
plt.title("Problem cost")

plt.show()


# Test no verbose
print(" TEST NO VERBOSE ")
results = lienlp.Results(nx, problem)
workspace = lienlp.Workspace(nx, nx, problem)
solver2 = lienlp.Solver(space, problem, mu_init=1e-6, verbose=False)
solver2.solve(workspace, results, x_init, lams0)
