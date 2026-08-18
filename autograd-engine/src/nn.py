import random

from src.engine import Value


class Linear:
    """Fully connected layer built from scalar Value objects."""

    def __init__(self, in_features, out_features, bias=True):
        self.in_features = in_features
        self.out_features = out_features
        self.bias_flag = bias
        self.weight = [
            [Value(random.uniform(-1.0, 1.0), label=f"w{out_idx}{in_idx}") for in_idx in range(in_features)]
            for out_idx in range(out_features)
        ]
        self.bias = [Value(0.0, label=f"b{idx}") for idx in range(out_features)] if bias else None

    def forward(self, x):
        if not isinstance(x, (list, tuple)):
            x = [x]
        if len(x) != self.in_features:
            raise ValueError(f"Expected {self.in_features} inputs, received {len(x)}.")

        outputs = []
        for out_idx in range(self.out_features):
            total = self.bias[out_idx] if self.bias_flag else Value(0.0)
            for in_idx in range(self.in_features):
                total = total + (x[in_idx] * self.weight[out_idx][in_idx])
            outputs.append(total)
        return outputs

    __call__ = forward


class ReLU:
    """Element-wise rectified linear unit."""

    def forward(self, values):
        if isinstance(values, Value):
            return values.relu()
        return [value.relu() for value in values]

    __call__ = forward


class MSELoss:
    """Mean squared error loss over scalar predictions."""

    def forward(self, predictions, targets):
        if len(predictions) != len(targets):
            raise ValueError("Predictions and targets must have the same length.")

        total = Value(0.0, label="loss_total")
        for pred, target in zip(predictions, targets):
            diff = pred - target
            total = total + (diff * diff)
        return total / len(predictions)

    __call__ = forward
