#include "proxnlp/blocks.hpp"

namespace proxnlp {
namespace block_chol {

/// BlockKind of the transpose of a matrix.
BlockKind trans(BlockKind a) noexcept {
  if ((a == TriL) || (a == TriU)) {
    return TriU;
  }
  return a;
}

/// BlockKind of the addition of two matrices - given by their BlockKind.
BlockKind add(BlockKind a, BlockKind b) noexcept {
  if (a == Dense || b == Dense || int(a) + int(b) == int(TriL) + int(TriU)) {
    return Dense;
  }
  return std::max(a, b);
}

/// BlockKind of the product of two matrices.
BlockKind mul(BlockKind a, BlockKind b) noexcept {
  if (a == Zero || b == Zero) {
    return Zero;
  }
  return block_chol::add(a, b);
}

/* IMPLS FOR SYMBOLIC MATRIX */

SymbolicBlockMatrix SymbolicBlockMatrix::submatrix(isize i,
                                                   isize n) const noexcept {
  return {
      ptr(i, i),
      segment_lens + i,
      n,
      outer_stride,
  };
}

void SymbolicBlockMatrix::deep_copy(SymbolicBlockMatrix const &in,
                                    isize const *perm) const noexcept {
  auto self = *this;

  isize n = self.nsegments();

  for (isize i = 0; i < n; ++i) {
    self.segment_lens[i] = in.segment_lens[(perm != nullptr) ? perm[i] : i];
  }
  for (isize i = 0; i < n; ++i) {
    for (isize j = 0; j < n; ++j) {
      if (perm == nullptr) {
        self(i, j) = in(i, j);
      } else {
        self(i, j) = in(perm[i], perm[j]);
      }
    }
  }
}

Eigen::ComputationInfo SymbolicBlockMatrix::brute_force_best_permutation(
    SymbolicBlockMatrix const &in, isize *best_perm, isize *iwork) const {
  isize n = in.nsegments();
  std::iota(iwork, iwork + n, isize(0));

  bool first_iter = true;
  isize best_perm_nnz = 0;

  // find best permutation
  do {
    deep_copy(in, iwork);
    if (!llt_in_place()) {
      return Eigen::NumericalIssue;
    }

    isize nnz = count_nnz();

    if (first_iter || nnz < best_perm_nnz) {
      std::memcpy(best_perm, iwork, std::size_t(n) * sizeof(isize));
      best_perm_nnz = nnz;
    }

    first_iter = false;
  } while (std::next_permutation(iwork, iwork + n));
  return Eigen::Success;
}

isize SymbolicBlockMatrix::count_nnz() const noexcept {
  auto self = *this;
  isize nnz = 0;
  isize n = nsegments();

  for (isize i = 0; i < n; ++i) {
    for (isize j = 0; j < n; ++j) {
      switch (self(i, j)) {
      case Zero:
        break;
      case Diag: {
        nnz += self.segment_lens[i];
        break;
      }
      case TriL:
      case TriU: {
        isize k = self.segment_lens[i];
        nnz += (k * (k + 1)) / 2;
        break;
      }
      case Dense: {
        nnz += self.segment_lens[i] * self.segment_lens[j];
      }
      }
    }
  }
  return nnz;
}

bool SymbolicBlockMatrix::llt_in_place() const noexcept {
  // assume `*this` is symmetric
  if (segments_count == 0) {
    return true;
  }

  auto self = *this;

  isize n = segments_count;

  // zero triu part
  for (isize j = 1; j < n; ++j) {
    self(0, j) = BlockKind::Zero;
  }

  switch (self(0, 0)) {
  case TriL:
  case TriU:
  case Zero:
    return false;
  case Dense: {
    self(0, 0) = TriL;
    for (isize i = 1; i < n; ++i) {
      switch (self(i, 0)) {
      case Zero:
      case Diag: {
        self(i, 0) = TriU;
        break;
      }
      case TriL: {
        self(i, 0) = Dense;
        break;
      }
      case TriU:
      case Dense:
        break;
      }
    }
  }
  case Diag: {
    // l00 unchanged
    // l10 unchanged
  }
  }

  // update l11
  for (isize i = 1; i < n; ++i) {
    self(i, i) =
        block_chol::add(self(i, i),
                        block_chol::mul( //
                            self(i, 0), block_chol::trans(self(i, 0))));

    for (isize j = i + 1; j < n; ++j) {

      self(i, j) =
          block_chol::add(self(i, j),
                          block_chol::mul( //
                              self(i, 0), block_chol::trans(self(j, 0))));

      self(j, i) = block_chol::trans(self(i, j));
    }
  }

  return submatrix(1, n - 1).llt_in_place();
}

/* UTILS */

void print_sparsity_pattern(const SymbolicBlockMatrix &smat) noexcept {
  isize nrows = 0;
  for (isize i = 0; i < smat.segments_count; ++i) {
    nrows += smat.segment_lens[i];
  }

  isize ncols = nrows;

  std::vector<bool> buf;
  buf.resize(std::size_t(nrows * ncols));

  isize handled_rows = 0;
  for (isize i = 0; i < smat.segments_count; ++i) {
    isize handled_cols = 0;
    for (isize j = 0; j < smat.segments_count; ++j) {
      switch (smat(i, j)) {
      case Zero:
        break;
      case Diag: {
        for (isize ii = 0; ii < smat.segment_lens[i]; ++ii) {
          buf[std::size_t((handled_rows + ii) * ncols + handled_cols + ii)] =
              true;
        }
        break;
      }
      case TriL: {
        for (isize ii = 0; ii < smat.segment_lens[i]; ++ii) {
          for (isize jj = 0; jj <= ii; ++jj) {
            buf[std::size_t((handled_rows + ii) * ncols + handled_cols + jj)] =
                true;
          }
        }
        break;
      }
      case TriU: {
        for (isize ii = 0; ii < smat.segment_lens[i]; ++ii) {
          for (isize jj = ii; jj < smat.segment_lens[j]; ++jj) {
            buf[std::size_t((handled_rows + ii) * ncols + handled_cols + jj)] =
                true;
          }
        }
        break;
      }
      case Dense: {
        for (isize ii = 0; ii < smat.segment_lens[i]; ++ii) {
          for (isize jj = 0; jj < smat.segment_lens[j]; ++jj) {
            buf[std::size_t((handled_rows + ii) * ncols + handled_cols + jj)] =
                true;
          }
        }
        break;
      }
      }
      handled_cols += smat.segment_lens[j];
    }
    handled_rows += smat.segment_lens[i];
  }

  for (isize i = 0; i < nrows; ++i) {
    for (isize j = 0; j < ncols; ++j) {
      if (buf[std::size_t(i * ncols + j)]) {
        std::cout << "█";
      } else {
        std::cout << "░";
      }
    }
    std::cout << '\n';
  }
}

void find_permutation(const SymbolicBlockMatrix &mat, isize *best_perm) {
  std::size_t n = std::size_t(mat.nsegments());
  std::size_t size = std::size_t(mat.size());
  auto copy_data = new BlockKind[size];
  auto copy_segments = new isize[n];

  // create a copy as a workspace
  SymbolicBlockMatrix copy_mat{copy_data, copy_segments, isize(n), isize(n)};

  isize *iwork = new isize[n];
  copy_mat.brute_force_best_permutation(mat, best_perm, iwork);
}

} // namespace block_chol
} // namespace proxnlp