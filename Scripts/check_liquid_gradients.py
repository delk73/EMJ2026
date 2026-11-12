"""Numerical cross-check of the shader's analytic gradient equations, not GPU/viewport QA.

Run with ordinary Python: python Scripts/check_liquid_gradients.py
Checks pinched capsule interiors/caps, compact support, overlap and degenerate endpoints.
"""
import math
import random


def field(point, start, end, radius, energy, pinch):
    """Scalar kernel only, used as an independent finite-difference reference."""
    ab = [end[i] - start[i] for i in range(2)]
    length2 = sum(x*x for x in ab)
    t = max(0, min(1, sum((point[i]-start[i])*ab[i] for i in range(2))/max(length2, 1e-6)))
    r = radius * max(0.02, 1 - pinch * (4*t*(1-t))**2)
    distance2 = sum((point[i] - start[i] - t*ab[i])**2 for i in range(2))
    return energy * max(0, 1-distance2/(r*r))**3


def gradient(point, start, end, radius, energy, pinch):
    ab = [end[i]-start[i] for i in range(2)]
    length2 = sum(x*x for x in ab)
    raw_t = sum((point[i]-start[i])*ab[i] for i in range(2))/max(length2, 1e-6)
    t = max(0, min(1, raw_t))
    grad_t = [x/length2 if 0 < raw_t < 1 and length2 > 1e-6 else 0 for x in ab]
    d = [point[i]-start[i]-t*ab[i] for i in range(2)]
    d2 = sum(x*x for x in d)
    factor = 1-pinch*16*t*t*(1-t)*(1-t)
    r = radius*max(0.02, factor)
    grad_r = [-radius*pinch*32*t*(1-t)*(1-2*t)*x if factor > 0.02 else 0 for x in grad_t]
    q = max(0, 1-d2/(r*r))
    return [3*energy*q*q*(-2*d[i]/(r*r)+2*d2*grad_r[i]/(r*r*r)) for i in range(2)]


def main():
    rng = random.Random(48)
    worst = 0
    samples = 0
    # Pinch=0 and coincident endpoints also check the radial node kernel.
    for start, end in (((-100, -30), (130, 90)), ((0, 0), (0, 0))):
        for pinch in (0, 0.3, 0.85, 1):
            for _ in range(1000):
                point = [rng.uniform(-180, 180), rng.uniform(-100, 150)]
                actual = gradient(point, start, end, 55, 0.65, pinch)
                for axis in range(2):
                    left, right = point.copy(), point.copy()
                    left[axis] -= 1e-4
                    right[axis] += 1e-4
                    expected = (field(right, start, end, 55, 0.65, pinch)-field(left, start, end, 55, 0.65, pinch))/2e-4
                    error = abs(actual[axis]-expected)
                    assert math.isfinite(actual[axis]) and error < 2e-5, (point, pinch, actual[axis], expected)
                    worst = max(worst, error)
                    samples += 1
    # Sum/gradient linearity means overlapping nodes and tethers obey the same derivative identity.
    assert field((10000, 10000), (-100, 0), (100, 0), 55, 0.65, 0) == 0
    print(f"ANALYTICAL_GRADIENTS_OK: {samples} partial derivatives; max absolute error {worst:.3g}")


if __name__ == "__main__":
    main()
