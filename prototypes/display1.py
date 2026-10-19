from sense_hat import SenseHat
import math
import time

sense = SenseHat()
sense.clear()

# ---------------------------------------------------------
# Configuration
# ---------------------------------------------------------

WIDTH = 8
HEIGHT = 8

# Rotation speed (degrees per frame)
ROTATION_SPEED = 6

# Translation speed
MOVE_SPEED = 0.08

# Time between frames
FRAME_TIME = 0.05

# X size
X_SIZE = 2.8

# Color of the X
X_COLOR = (255, 0, 0)


# ---------------------------------------------------------
# Create points making up an X
# ---------------------------------------------------------

def create_x(size):
    """
    Create points along the two diagonals of an X.

    The X is centered at (0, 0).
    """
    points = []

    steps = 80

    for i in range(steps + 1):
        t = -size + (2 * size * i / steps)

        # First diagonal: \
        points.append((t, t))

        # Second diagonal: /
        points.append((t, -t))

    return points


# ---------------------------------------------------------
# Rotate a point
# ---------------------------------------------------------

def rotate_point(x, y, angle):
    """
    Rotate (x, y) around the origin.
    Angle is in radians.
    """
    cos_a = math.cos(angle)
    sin_a = math.sin(angle)

    new_x = x * cos_a - y * sin_a
    new_y = x * sin_a + y * cos_a

    return new_x, new_y


# ---------------------------------------------------------
# Draw the X
# ---------------------------------------------------------

def draw_x(points, angle, center_x, center_y):
    """
    Rotate and translate the X, then draw it on the
    8x8 Sense HAT display.
    """

    pixels = [[(0, 0, 0) for _ in range(WIDTH)]
              for _ in range(HEIGHT)]

    for x, y in points:

        # Rotate
        rx, ry = rotate_point(x, y, angle)

        # Translate
        px = rx + center_x
        py = ry + center_y

        # Convert to integer LED coordinates
        ix = round(px)
        iy = round(py)

        # Draw the pixel if it is on the display
        if 0 <= ix < WIDTH and 0 <= iy < HEIGHT:
            pixels[iy][ix] = X_COLOR

    # Flatten the 2D array for Sense HAT
    image = []

    for row in pixels:
        image.extend(row)

    sense.set_pixels(image)


# ---------------------------------------------------------
# Main animation
# ---------------------------------------------------------

points = create_x(X_SIZE)

angle = 0

# Start near the center
center_x = 3.5
center_y = 3.5

# Direction of movement
vx = MOVE_SPEED
vy = MOVE_SPEED * 0.7


try:
    while True:

        # Draw current frame
        draw_x(
            points,
            angle,
            center_x,
            center_y
        )

        # Continuous rotation
        angle += math.radians(ROTATION_SPEED)

        # Continuous translation
        center_x += vx
        center_y += vy

        # Bounce off the edges
        #
        # The X is allowed to partially leave the display,
        # making the translation more obvious.

        if center_x > 6.0:
            vx = -abs(vx)

        elif center_x < 1.0:
            vx = abs(vx)

        if center_y > 6.0:
            vy = -abs(vy)

        elif center_y < 1.0:
            vy = abs(vy)

        time.sleep(FRAME_TIME)


except KeyboardInterrupt:
    sense.clear()