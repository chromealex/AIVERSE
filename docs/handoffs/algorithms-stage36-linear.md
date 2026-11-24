# Stage 36
FixedLinear.h adds bounded Gauss-Jordan solve, inverse, pivoted packed LU and determinant. Q16, partial pivoting, dimensions 1–16, rejects small pivots and out-of-range intermediates. Singular determinant returns nullopt rather than zero. No build/tests. Next LU solve, residual checks and catalog update. Branch feature/world-processes-stage31-bidirectional-handles
