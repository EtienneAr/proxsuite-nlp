/// @file
/// @brief     Utility function to allocate an LDLT solver for the Newton
/// iterations.
/// @copyright Copyright (C) 2022 LAAS-CNRS, INRIA
#pragma once

#include "linalg/blocks.hpp"
#include "proxnlp/problem-base.hpp"

namespace proxnlp {

namespace {
using linalg::BlockKind;
using linalg::BlockLDLT;
using linalg::isize;
using linalg::SymbolicBlockMatrix;
} // namespace

enum class LDLTChoice {
  /// Use our dense LDLT.
  DENSE,
  /// Use blocked LDLT.
  BLOCKED,
  /// Use Eigen's implementation.
  EIGEN,
};

template <typename Scalar>
BlockLDLT<Scalar> *
allocate_block_ldlt_from_structure(const std::vector<isize> &nprims,
                                   const std::vector<isize> &nduals) {
  const isize nprim_blocks = (isize)nprims.size();
  const isize nblocks = nprim_blocks + (isize)(nduals.size());

  isize tot_size = std::accumulate(nprims.begin(), nprims.end(), isize(0)) +
                   std::accumulate(nduals.begin(), nduals.end(), isize(0));

  std::vector<BlockKind> blocks((std::size_t)(nblocks * nblocks));
  std::vector<long> seg_lens = nprims;
  for (auto &i : nduals) {
    seg_lens.push_back(i);
  }

  SymbolicBlockMatrix structure{blocks.data(), seg_lens.data(), nblocks,
                                nblocks, false};

  // default structure: primal blocks are dense, others are sparse

  for (isize i = 0; i < nprim_blocks; ++i) {
    for (isize j = 0; j < nprim_blocks; ++j) {
      structure(i, j) = BlockKind::Dense;
    }
  }

  for (isize i = nprim_blocks; i < nblocks; ++i) {
    // first col/row
    structure(i, nprim_blocks) = BlockKind::Dense;
    structure(nprim_blocks, i) = BlockKind::Dense;

    for (isize j = nprim_blocks; j < nblocks; ++j) {
      structure(i, j) = BlockKind::Zero;
    }

    // diag
    structure(i, i) = BlockKind::Diag;
  }

  BlockLDLT<Scalar> *ldlt = new BlockLDLT<Scalar>(tot_size, structure.copy());
  (*ldlt).findSparsifyingPermutation().updateBlockPermutationMatrix(structure);
  return ldlt;
}

template <typename Scalar>
unique_ptr<linalg::ldlt_base<Scalar>>
allocate_ldlt_from_sizes(const std::vector<isize> &nprims,
                         const std::vector<isize> &nduals, LDLTChoice choice) {
  using ldlt_ptr_t = unique_ptr<linalg::ldlt_base<Scalar>>;
  const long size = std::accumulate(nprims.begin(), nprims.end(), isize(0)) +
                    std::accumulate(nduals.begin(), nduals.end(), isize(0));
  switch (choice) {
  case LDLTChoice::DENSE:
    return ldlt_ptr_t(new linalg::DenseLDLT<Scalar>(size));
  case LDLTChoice::BLOCKED: {
    return ldlt_ptr_t(
        allocate_block_ldlt_from_structure<Scalar>(nprims, nduals));
  }
  case LDLTChoice::EIGEN:
    return ldlt_ptr_t(new linalg::EigenLDLTWrapper<Scalar>(size));
  default:
    return nullptr;
  }
}

template <typename Scalar>
unique_ptr<linalg::ldlt_base<Scalar>>
allocate_ldlt_from_problem(const ProblemTpl<Scalar> &prob, LDLTChoice choice) {
  std::vector<isize> nduals(prob.getNumConstraints());
  for (std::size_t i = 0; i < nduals.size(); ++i)
    nduals[i] = prob.getConstraintDim(i);
  return allocate_ldlt_from_sizes<Scalar>({prob.ndx()}, nduals, choice);
}

} // namespace proxnlp