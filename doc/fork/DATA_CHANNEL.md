# Data Channel

<img src="images/icons/mod_data_channel.png" width="32" height="32" alt="Data Channel">

Computes a value for each point, such as its height, convexity or edge length, remaps it, and
writes it to an attribute or a vertex group, or moves the points by it. Other modifiers and
materials can use the result: for example, dirt in hollows from **Convexity**, or a vertex group
that fades with height.

Add it from **Add Modifier ▸ Edit**. It works on meshes. In the [Modifier List](MODIFIER_LIST.md)'s Add menu it's in the **Data** column.

## Settings

- **Source**: What value to compute for each point.
  - **Position X**
  - **Position Y**
  - **Position Z**
  - **Normal X**
  - **Normal Y**
  - **Normal Z**
  - **Convexity**: Positive on outward bumps and negative in hollows.
  - **Edge Length**: Average length of the edges around each point.
  - **Face Area**: Average area of the faces around each point.
  - **Random**: A random value for each point.
  - **Distance from Center**: Distance from the center of the bounding box.
- **Output**: Where to put the values.
  - **Attribute**: Write a point attribute.
  - **Vertex Group**: Write a vertex group, clamped between 0 and 1.
  - **Displace**: Move the points along their normals by the value.
- **Seed**: Changes the random values.
- **Normalize**: Remap the values so the smallest is 0 and the largest is 1.
- **Invert**: Invert the values.
- **Power**: Raise the values to this power.
- **Multiply**: Multiply the values.
- **Add**: Add to the values.
- **Clamp**: Keep the values between 0 and 1.

## As a node

Geometry Nodes has a **Data Channel** node that shares its code with this modifier, so a node tree and the modifier give the same result.

## In Python

```python
mod = obj.modifiers.new("Data Channel", 'DATA_CHANNEL')
```

The Python type is `DataChannelModifier`. Official Blender doesn't know this modifier, so a file that uses it won't give the same result there.
