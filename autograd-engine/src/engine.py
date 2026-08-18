import math


class Value:
    """Scalar value with auto-differentiation support."""

    def __init__(self, data, _children=(), op="", label=""):
        self.data = float(data)
        self.grad = 0.0
        self.label = label
        self._prev = tuple(_children)
        self._op = op
        self._backward = lambda: None

    def __repr__(self):
        return f"Value(data={self.data}, grad={self.grad})"

    def __add__(self, other):
        other = _as_value(other)
        out = Value(self.data + other.data, (self, other), "+")

        def _backward():
            self.grad += out.grad
            other.grad += out.grad

        out._backward = _backward
        return out

    def __radd__(self, other):
        return self + other

    def __mul__(self, other):
        other = _as_value(other)
        out = Value(self.data * other.data, (self, other), "*")

        def _backward():
            self.grad += other.data * out.grad
            other.grad += self.data * out.grad

        out._backward = _backward
        return out

    def __rmul__(self, other):
        return self * other

    def __neg__(self):
        return self * -1

    def __sub__(self, other):
        return self + (-_as_value(other))

    def __rsub__(self, other):
        return _as_value(other) - self

    def __truediv__(self, other):
        other = _as_value(other)
        out = Value(self.data / other.data, (self, other), "/")

        def _backward():
            self.grad += (1.0 / other.data) * out.grad
            other.grad += (-(self.data / (other.data ** 2))) * out.grad

        out._backward = _backward
        return out

    def __rtruediv__(self, other):
        return _as_value(other) / self

    def __pow__(self, exponent):
        exponent = float(exponent)
        out = Value(self.data ** exponent, (self,), f"**{exponent}")

        def _backward():
            self.grad += (exponent * (self.data ** (exponent - 1))) * out.grad

        out._backward = _backward
        return out

    def relu(self):
        out = Value(max(0.0, self.data), (self,), "ReLU")

        def _backward():
            self.grad += (1.0 if self.data > 0 else 0.0) * out.grad

        out._backward = _backward
        return out

    def exp(self):
        out = Value(math.exp(self.data), (self,), "exp")

        def _backward():
            self.grad += out.data * out.grad

        out._backward = _backward
        return out

    def backward(self):
        topo = []
        visited = set()

        def build(node):
            if id(node) not in visited:
                visited.add(id(node))
                for previous in node._prev:
                    build(previous)
                topo.append(node)

        build(self)
        self.grad = 1.0
        for node in reversed(topo):
            node._backward()


def _as_value(value):
    if isinstance(value, Value):
        return value
    return Value(value)
