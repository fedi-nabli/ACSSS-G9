
from sense_hat import SenseHat
import math
import time


# ============================================================
# 3D CROSS DEMONSTRATION
#
# A 3D coordinate cross is projected onto the 8x8 Sense HAT
# LED matrix.
#
# X axis = red/orange
# Y axis = green
# Z axis = blue/purple
#
# The entire cross can be rotated around X, Y and Z and
# translated along X, Y and Z.
# ============================================================


sense = SenseHat()
sense.clear()


# ------------------------------------------------------------
# Display settings
# ------------------------------------------------------------

WIDTH = 8
HEIGHT = 8

FPS = 30
FRAME_TIME = 1.0 / FPS


# ------------------------------------------------------------
# Colors
# ------------------------------------------------------------

RED = (255, 30, 30)
GREEN = (30, 255, 30)
BLUE = (30, 80, 255)

YELLOW = (255, 220, 30)
CYAN = (30, 255, 255)
PURPLE = (200, 30, 255)

OFF = (0, 0, 0)


# ============================================================
# 3D CROSS
# ============================================================

# Each point is:
#
#     (x, y, z, color_axis)
#
# color_axis:
#     0 = X
#     1 = Y
#     2 = Z
#
# The cross is deliberately asymmetric so its orientation
# is easy to recognize.
# ============================================================


def create_cross():

    points = []

    # --------------------------------------------------------
    # X AXIS
    #
    # Longer horizontal arm
    # --------------------------------------------------------

    for x in range(-5, 6):

        points.append(
            (x, 0, 0, 0)
        )


    # --------------------------------------------------------
    # Y AXIS
    #
    # Slightly shorter vertical arm
    # --------------------------------------------------------

    for y in range(-3, 4):

        points.append(
            (0, y, 0, 1)
        )


    # --------------------------------------------------------
    # Z AXIS
    #
    # Extends toward/away from viewer.
    #
    # This axis is represented using the color gradient.
    # --------------------------------------------------------

    for z in range(-3, 4):

        points.append(
            (0, 0, z, 2)
        )


    return points


CROSS = create_cross()


# ============================================================
# ROTATION
# ============================================================

def rotate_x(point, angle):

    x, y, z = point

    c = math.cos(angle)
    s = math.sin(angle)

    return (
        x,
        y * c - z * s,
        y * s + z * c
    )


def rotate_y(point, angle):

    x, y, z = point

    c = math.cos(angle)
    s = math.sin(angle)

    return (
        x * c + z * s,
        y,
        -x * s + z * c
    )


def rotate_z(point, angle):

    x, y, z = point

    c = math.cos(angle)
    s = math.sin(angle)

    return (
        x * c - y * s,
        x * s + y * c,
        z
    )


def rotate_point(point, rx, ry, rz):

    point = rotate_x(point, rx)
    point = rotate_y(point, ry)
    point = rotate_z(point, rz)

    return point


# ============================================================
# TRANSLATION
# ============================================================

def translate_point(point, tx, ty, tz):

    x, y, z = point

    return (
        x + tx,
        y + ty,
        z + tz
    )


# ============================================================
# PERSPECTIVE PROJECTION
# ============================================================

def project(point):

    x, y, z = point

    # Distance between camera and object
    CAMERA_DISTANCE = 7.0

    camera_z = z + CAMERA_DISTANCE

    # Prevent division by zero
    camera_z = max(camera_z, 0.5)

    # Perspective
    scale = 4.0 / camera_z

    screen_x = x * scale
    screen_y = y * scale

    # Center on 8x8 display
    pixel_x = screen_x + 3.5
    pixel_y = screen_y + 3.5

    return pixel_x, pixel_y, z


# ============================================================
# COLOR FUNCTIONS
# ============================================================

def blend(c1, c2, amount):

    amount = max(
        0.0,
        min(1.0, amount)
    )

    return (
        int(c1[0] * (1 - amount) + c2[0] * amount),
        int(c1[1] * (1 - amount) + c2[1] * amount),
        int(c1[2] * (1 - amount) + c2[2] * amount)
    )


def get_color(axis, z):

    # --------------------------------------------------------
    # X axis
    # --------------------------------------------------------

    if axis == 0:

        return RED


    # --------------------------------------------------------
    # Y axis
    # --------------------------------------------------------

    if axis == 1:

        return GREEN


    # --------------------------------------------------------
    # Z axis
    #
    # Map depth onto a gradient:
    #
    # far:
    #     blue
    #
    # middle:
    #     cyan
    #
    # near:
    #     purple
    # --------------------------------------------------------

    # Normalize z
    depth = (z + 3.0) / 6.0

    depth = max(
        0.0,
        min(1.0, depth)
    )


    if depth < 0.5:

        t = depth / 0.5

        return blend(
            BLUE,
            CYAN,
            t
        )

    else:

        t = (depth - 0.5) / 0.5

        return blend(
            CYAN,
            PURPLE,
            t
        )


# ============================================================
# DRAW A POINT WITH SOFTNESS
# ============================================================

def draw_point(
    x,
    y,
    color,
    pixels,
    strength=1.0
):

    # --------------------------------------------------------
    # Bilinear-ish brightness distribution.
    #
    # This makes subpixel movement look smoother.
    # --------------------------------------------------------

    x0 = math.floor(x)
    y0 = math.floor(y)

    fx = x - x0
    fy = y - y0


    neighbors = [
        (x0,     y0,     (1 - fx) * (1 - fy)),
        (x0 + 1, y0,     fx * (1 - fy)),
        (x0,     y0 + 1, (1 - fx) * fy),
        (x0 + 1, y0 + 1, fx * fy),
    ]


    for px, py, weight in neighbors:

        if not (
            0 <= px < WIDTH and
            0 <= py < HEIGHT
        ):
            continue


        brightness = (
            weight *
            strength
        )

        old = pixels[py][px]


        # Additive color blending
        pixels[py][px] = (
            min(
                255,
                int(old[0] + color[0] * brightness)
            ),
            min(
                255,
                int(old[1] + color[1] * brightness)
            ),
            min(
                255,
                int(old[2] + color[2] * brightness)
            )
        )


# ============================================================
# RENDER
# ============================================================

def render(
    rx,
    ry,
    rz,
    tx,
    ty,
    tz
):

    pixels = [
        [OFF for _ in range(WIDTH)]
        for _ in range(HEIGHT)
    ]


    # --------------------------------------------------------
    # Transform every point
    # --------------------------------------------------------

    transformed = []


    for x, y, z, axis in CROSS:

        point = (
            x,
            y,
            z
        )


        # Rotate
        point = rotate_point(
            point,
            rx,
            ry,
            rz
        )


        # Translate
        point = translate_point(
            point,
            tx,
            ty,
            tz
        )


        # Project
        px, py, depth = project(
            point
        )


        color = get_color(
            axis,
            z
        )


        transformed.append(
            (
                px,
                py,
                depth,
                color
            )
        )


    # --------------------------------------------------------
    # Draw farther points first.
    #
    # This gives us a simple depth ordering.
    # --------------------------------------------------------

    transformed.sort(
        key=lambda p: p[2],
        reverse=True
    )


    for px, py, depth, color in transformed:

        # Points closer to the camera are brighter.
        brightness = 0.55 + (
            0.45 *
            max(
                0.0,
                min(
                    1.0,
                    1.0 - depth / 5.0
                )
            )
        )


        color = (
            int(color[0] * brightness),
            int(color[1] * brightness),
            int(color[2] * brightness)
        )


        draw_point(
            px,
            py,
            color,
            pixels
        )


    # --------------------------------------------------------
    # Display
    # --------------------------------------------------------

    output = []

    for row in pixels:

        output.extend(row)

    sense.set_pixels(output)


# ============================================================
# SMOOTH INTERPOLATION
# ============================================================

def smoothstep(t):

    return (
        t * t *
        (3.0 - 2.0 * t)
    )


def interpolate(a, b, t):

    t = smoothstep(t)

    return (
        a +
        (b - a) * t
    )


# ============================================================
# ANIMATION
# ============================================================

def animate(
    start_rx,
    end_rx,

    start_ry,
    end_ry,

    start_rz,
    end_rz,

    start_tx,
    end_tx,

    start_ty,
    end_ty,

    start_tz,
    end_tz,

    duration
):

    start_time = time.monotonic()


    while True:

        elapsed = (
            time.monotonic() -
            start_time
        )


        t = elapsed / duration

        t = min(
            1.0,
            t
        )


        rx = interpolate(
            start_rx,
            end_rx,
            t
        )

        ry = interpolate(
            start_ry,
            end_ry,
            t
        )

        rz = interpolate(
            start_rz,
            end_rz,
            t
        )

        tx = interpolate(
            start_tx,
            end_tx,
            t
        )

        ty = interpolate(
            start_ty,
            end_ty,
            t
        )

        tz = interpolate(
            start_tz,
            end_tz,
            t
        )


        render(
            rx,
            ry,
            rz,
            tx,
            ty,
            tz
        )


        if t >= 1.0:
            break


        time.sleep(
            FRAME_TIME
        )


# ============================================================
# DEMO
# ============================================================

def run_demo():

    rx = 0.0
    ry = 0.0
    rz = 0.0

    tx = 0.0
    ty = 0.0
    tz = 0.0


    while True:

        # ====================================================
        # 1. Rotate around X
        # ====================================================

        animate(
            rx,
            rx + math.radians(360),

            ry,
            ry,

            rz,
            rz,

            tx,
            tx,

            ty,
            ty,

            tz,
            tz,

            3.0
        )

        rx += math.radians(360)


        # ====================================================
        # 2. Rotate around Y
        # ====================================================

        animate(
            rx,
            rx,

            ry,
            ry + math.radians(360),

            rz,
            rz,

            tx,
            tx,

            ty,
            ty,

            tz,
            tz,

            3.0
        )

        ry += math.radians(360)


        # ====================================================
        # 3. Rotate around Z
        # ====================================================

        animate(
            rx,
            rx,

            ry,
            ry,

            rz,
            rz + math.radians(360),

            tx,
            tx,

            ty,
            ty,

            tz,
            tz,

            3.0
        )

        rz += math.radians(360)


        # ====================================================
        # 4. Combined XYZ rotation
        # ====================================================

        animate(
            rx,
            rx + math.radians(360),

            ry,
            ry + math.radians(540),

            rz,
            rz + math.radians(720),

            tx,
            tx,

            ty,
            ty,

            tz,
            tz,

            5.0
        )

        rx += math.radians(360)
        ry += math.radians(540)
        rz += math.radians(720)


        # ====================================================
        # 5. Translate RIGHT
        # ====================================================

        animate(
            rx,
            rx + math.radians(180),

            ry,
            ry + math.radians(180),

            rz,
            rz,

            tx,
            tx + 2.5,

            ty,
            ty,

            tz,
            tz,

            2.0
        )

        rx += math.radians(180)
        ry += math.radians(180)
        tx += 2.5


        # ====================================================
        # 6. Translate LEFT
        # ====================================================

        animate(
            rx,
            rx + math.radians(360),

            ry,
            ry + math.radians(360),

            rz,
            rz + math.radians(180),

            tx,
            tx - 5.0,

            ty,
            ty,

            tz,
            tz,

            3.0
        )

        rx += math.radians(360)
        ry += math.radians(360)
        rz += math.radians(180)
        tx -= 5.0


        # ====================================================
        # 7. Translate UP
        # ====================================================

        animate(
            rx,
            rx + math.radians(360),

            ry,
            ry,

            rz,
            rz + math.radians(360),

            tx,
            tx,

            ty,
            ty - 3.0,

            tz,
            tz,

            2.0
        )

        rx += math.radians(360)
        rz += math.radians(360)
        ty -= 3.0


        # ====================================================
        # 8. Translate DOWN
        # ====================================================

        animate(
            rx,
            rx + math.radians(360),

            ry,
            ry + math.radians(360),

            rz,
            rz,

            tx,
            tx,

            ty,
            ty + 6.0,

            tz,
            tz,

            3.0
        )

        rx += math.radians(360)
        ry += math.radians(360)
        ty += 6.0


        # ====================================================
        # 9. Move toward camera
        # ====================================================

        animate(
            rx,
            rx + math.radians(360),

            ry,
            ry + math.radians(360),

            rz,
            rz + math.radians(360),

            tx,
            tx,

            ty,
            ty,

            tz,
            tz - 3.0,

            3.0
        )

        rx += math.radians(360)
        ry += math.radians(360)
        rz += math.radians(360)
        tz -= 3.0


        # ====================================================
        # 10. Move away from camera while rotating
        # ====================================================

        animate(
            rx,
            rx + math.radians(720),

            ry,
            ry + math.radians(720),

            rz,
            rz + math.radians(720),

            tx,
            0.0,

            ty,
            0.0,

            tz,
            0.0,

            4.0
        )

        rx += math.radians(720)
        ry += math.radians(720)
        rz += math.radians(720)

        tx = 0.0
        ty = 0.0
        tz = 0.0


# ============================================================
# MAIN
# ============================================================

try:

    print("3D Cross demonstration")
    print("Press Ctrl+C to stop.")

    run_demo()

except KeyboardInterrupt:

    print("\nDemo stopped.")

finally:

    sense.clear()
