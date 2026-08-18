# Autograd Engine

## Why this project exists

This repository is a compact study in automatic differentiation for a university-level systems project. The goal is to rebuild the core ideas behind modern deep-learning frameworks in plain Python: a scalar value object, a computational graph, reverse-mode differentiation, and a few standard neural-network primitives. The educational purpose is not to replace PyTorch, but to expose the mechanics behind it in a way that is transparent, inspectable, and easy to verify mathematically.

## Mathematical breakdown

The engine models each scalar as a `Value` node that stores both its numeric value and the local derivative with respect to any upstream operation. When the graph is built, every node retains a reference to its parents and a closure that defines how gradients are propagated.

For an addition node,

$$
 z = x + y
$$

we can compute the local Jacobian as

$$
 \frac{\partial z}{\partial x} = 1, \quad \frac{\partial z}{\partial y} = 1
$$

so the backward pass accumulates:

$$
 \frac{\partial L}{\partial x} += \frac{\partial L}{\partial z}, \quad \frac{\partial L}{\partial y} += \frac{\partial L}{\partial z}
$$

For multiplication,

$$
 z = x \cdot y
$$

we use the product rule:

$$
 \frac{\partial z}{\partial x} = y, \quad \frac{\partial z}{\partial y} = x
$$

so the gradients propagate as

$$
 \frac{\partial L}{\partial x} += y \cdot \frac{\partial L}{\partial z}, \quad \frac{\partial L}{\partial y} += x \cdot \frac{\partial L}{\partial z}
$$

This same pattern generalizes to a full computational graph. The engine performs a topological traversal from leaves to roots, then walks backward through the reverse ordering to accumulate gradients for every node. That is exactly the reverse-mode differentiation used by modern frameworks.

The loss for a simple regression problem is the mean squared error:

$$
 \mathcal{L}(\hat{y}, y) = \frac{1}{n} \sum_{i=1}^{n} (\hat{y}_i - y_i)^2
$$

Its derivative with respect to each prediction is:

$$
 \frac{\partial \mathcal{L}}{\partial \hat{y}_i} = \frac{2}{n}(\hat{y}_i - y_i)
$$

which is the rule the engine reproduces when it evaluates the loss and calls `backward()`.

## Quickstart

Run the following three commands to clone the repository, install the dependencies, and verify the implementation with the test suite:

```bash
git clone https://github.com/your-username/autograd-engine.git
cd autograd-engine
pip install -r requirements.txt && pytest -q
```

## Tradeoffs and limitations

This project is intentionally small and educational. It implements scalar autodiff in pure Python, which makes it easy to reason about but far slower than production systems. In a production stack, the same ideas are accelerated with compiled kernels, fused operations, and specialized memory layouts. For example, matrix multiplications would normally be dispatched to optimized BLAS or CUDA routines instead of Python-level loops, which is why frameworks like PyTorch can train large models efficiently.

The engine also omits features that production frameworks include: batching, GPU execution, optimizer states, automatic mixed precision, sparse tensors, and efficient memory reuse. The project is therefore best viewed as a transparent reference implementation that demonstrates the mathematics and data flow behind established tools rather than a drop-in replacement for a serious training stack.

## Project structure

```text
autograd-engine/
├── src/
│   ├── __init__.py
│   ├── engine.py
│   └── nn.py
├── tests/
│   └── test_autograd.py
├── notebooks/
├── requirements.txt
├── README.md
└── .gitignore
```

## Validation

The repo includes tests that compare the custom gradient computations against PyTorch for the same arithmetic and a small linear model. The test suite proves that reverse-mode autodiff is implemented correctly up to a tight numerical tolerance.
