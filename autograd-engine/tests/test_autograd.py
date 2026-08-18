import math

import pytest
import torch

from src.engine import Value
from src.nn import Linear, MSELoss, ReLU


def test_value_backward_for_simple_expression():
    x = Value(2.0, label="x")
    y = Value(3.0, label="y")
    z = (x * y) + (x * x)
    z.backward()

    assert z.data == pytest.approx(10.0)
    assert x.grad == pytest.approx(7.0)
    assert y.grad == pytest.approx(2.0)


def test_custom_gradients_match_pytorch():
    torch.manual_seed(0)

    x_torch = torch.tensor([1.0, -2.0], requires_grad=True)
    w_torch = torch.tensor([[0.5, -1.0], [-0.25, 1.5]], requires_grad=True)
    b_torch = torch.tensor([0.1, -0.2], requires_grad=True)
    target_torch = torch.tensor([1.0, 0.0])

    y_torch = x_torch @ w_torch.T + b_torch
    loss_torch = ((y_torch - target_torch) ** 2).mean()
    loss_torch.backward()

    x_custom = [Value(v, label=f"x{i}") for i, v in enumerate(x_torch.tolist())]
    w_custom = [
        [Value(v, label=f"w{row}_{col}") for col, v in enumerate(w_torch[row].tolist())]
        for row in range(w_torch.shape[0])
    ]
    b_custom = [Value(v, label=f"b{i}") for i, v in enumerate(b_torch.tolist())]

    model = Linear(2, 2)
    model.weight = w_custom
    model.bias = b_custom
    y_custom = model.forward(x_custom)

    target_custom = [Value(v, label=f"t{i}") for i, v in enumerate(target_torch.tolist())]
    loss_custom = MSELoss().forward(y_custom, target_custom)
    loss_custom.backward()

    assert loss_custom.data == pytest.approx(loss_torch.item(), rel=1e-5, abs=1e-5)
    for i, value in enumerate(x_custom):
        assert value.grad == pytest.approx(x_torch.grad[i].item(), rel=1e-5, abs=1e-5)

    for row in range(len(w_custom)):
        for col in range(len(w_custom[row])):
            assert w_custom[row][col].grad == pytest.approx(w_torch.grad[row, col].item(), rel=1e-5, abs=1e-5)

    for i, value in enumerate(b_custom):
        assert value.grad == pytest.approx(b_torch.grad[i].item(), rel=1e-5, abs=1e-5)
