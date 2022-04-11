#pragma once


#include "lienlp/macros.hpp"
#include "lienlp/cost-function.hpp"

#include <ostream>


namespace lienlp
{

  template<typename _Scalar>
  struct CostSum : CostFunctionBaseTpl<_Scalar>
  {
  public:
    using Scalar = _Scalar;
    LIENLP_DYNAMIC_TYPEDEFS(Scalar)
    using Base = CostFunctionBaseTpl<Scalar>;
    using BasePtr = Base const*;

    std::vector<BasePtr> m_components; /// component sub-costs
    std::vector<Scalar> m_weights; /// cost component weights

    CostSum(int nx, int ndx) : Base(nx, ndx) {}

    /// Constructor with a predefined vector of components.
    CostSum(int nx, int ndx,
            const std::vector<BasePtr>& comps,
            const std::vector<Scalar>& weights)
            : Base(nx, ndx)
            , m_components(comps)
            , m_weights(weights)
    {
      assert(m_components.size() == m_weights.size());
    }

    std::size_t numComponents() const
    {
      return m_components.size();
    }

    Scalar call(const ConstVectorRef& x) const
    {
      Scalar result_ = 0.;
      for (std::size_t i = 0; i < numComponents(); i++)
      {
        result_ += m_weights[i] * m_components[i]->call(x);
      }
      return result_;
    }

    void computeGradient(const ConstVectorRef& x, VectorRef out) const
    {
      out.setZero();
      for (std::size_t i = 0; i < numComponents(); i++)
      {
        out.noalias() = out + m_weights[i] * m_components[i]->computeGradient(x);
      }
    }

    void computeHessian(const ConstVectorRef& x, MatrixRef out) const
    {
      out.setZero();
      for (std::size_t i = 0; i < numComponents(); i++)
      {
        out.noalias() = out + m_weights[i] * m_components[i]->computeHessian(x);
      }
    }

    /* CostSum API definition */

    void addComponent(const Base& comp, const Scalar w = 1.)
    {
      m_components.push_back(&comp);
      m_weights.push_back(w);
    }

    CostSum<Scalar>& operator+=(const Base& other)
    {
      addComponent(other);
      return *this;
    }

    CostSum<Scalar>& operator+=(const CostSum<Scalar>& other)
    {
      m_components.insert(m_components.end(), other.m_components.begin(), other.m_components.end());
      m_weights.insert(m_weights.end(), other.m_weights.begin(), other.m_weights.end());
      return *this;
    }

    // increment this using an rvalue (for ex tmp rhs)
    CostSum<Scalar>& operator+=(CostSum<Scalar>&& rhs)
    {
      (*this) += std::forward<CostSum<Scalar>>(rhs);
      return *this;
    }

    CostSum<Scalar>& operator*=(const Scalar rhs)
    {
      for (auto& weight : m_weights)
      {
        weight = rhs * weight;
      }
      return *this;
    }

    // printing
    friend std::ostream& operator<<(std::ostream &ostr, const CostSum<Scalar>& cost)
    {
      const std::size_t nc = cost.numComponents();
      ostr << "CostSum(num_components=" << nc;
      ostr << ", weights=(";
      for (std::size_t i = 0; i < nc; i++)
      {
        ostr << cost.m_weights[i];
        if (i < nc - 1) ostr << ", ";
      }
      ostr << ")";
      ostr << ")";
      return ostr;
    }

  };

  template<typename Scalar>
  CostSum<Scalar> operator+(const CostFunctionBaseTpl<Scalar>& left, const CostFunctionBaseTpl<Scalar>& right)
  {
    assert((left.nx() == right.nx()) && (left.ndx() == right.ndx()) &&
           "Left and right should have the same input spaces.");
    CostSum<Scalar> out(left.nx(), left.ndx());
    out += left;
    out += right;
    return out;
  }

  // left is rvalue reference, so we modify it, return a move of the left after adding right
  template<typename Scalar>
  CostSum<Scalar>&& operator+(CostSum<Scalar>&& left, const CostFunctionBaseTpl<Scalar>& right)
  {
    left += right;
    return std::move(left);
  }

  // create a CostSum object with the desired weight
  template<typename Scalar>
  CostSum<Scalar> operator*(const Scalar& left, const CostFunctionBaseTpl<Scalar>& right)
  {
    CostSum<Scalar> out(right.nx(), right.ndx());
    out.addComponent(right, left);
    return out;
  }

  template<typename Scalar>
  CostSum<Scalar>&& operator*(Scalar&& left, CostSum<Scalar>&& right)
  {
    right *= left;
    return std::move(right);
  }

  template<typename Scalar>
  inline
  CostSum<Scalar>&& operator*(CostSum<Scalar>&& left, Scalar&& right)
  {
    return right * left;
  }
} // namespace lienlp

#include "lienlp/cost-sum.hxx"