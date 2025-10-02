---
author: Chris Conway (Koderz)
description: Documentation for the Realtime Mesh Component
generator:
- Hugo 0.111.3
- Relearn 5.17.1+tip
lang: en
robots: noindex, nofollow, noarchive, noimageindex
viewport: height=device-height, width=device-width, initial-scale=1.0,
  minimum-scale=1.0
---

::: {#body .default-animation}
::: {#sidebar-overlay}
:::

::: {#toc-overlay}
:::

<div>

::: {#breadcrumbs}
[ [](# "Menu (CTRL+ALT+n)"){#sidebar-toggle .topbar-link}
]{#sidebar-toggle-span}

1.  [Realtime Mesh Docs]{itemprop="name"}
:::

</div>

::: {#body-inner .highlightable .home role="main" tabindex="-1"}
::: flex-block-wrapper
::: headline
:::

# 

# Welcome to the Realtime Mesh Component

------------------------------------------------------------------------

Designed to enable rendering of realtime created, user loaded, runtime
modified, and any other type of mesh not possible to package into your
project! Scalable from simple mesh loading, up to full voxel terrain
worlds and everything in between.

Faster and more memory efficient than ProceduralMeshComponent, and
DynamicMeshComponent, with coming support for rendering UDynamicMesh to
be a full replacement to DynamicMeshComponent while keeping Geometry
Scripting tools.

------------------------------------------------------------------------

Core Features

-   Full Collision Support, both static triangle mesh and dynamic moving
    objects
-   Variable mesh formats, allowing for tradeoff in needed features and
    memory/performance overhead
-   Up to 8 Texture Coordinate (UV) channels
-   Normal or High precision Texture Coordinate (UV) channels
-   Normal or High precision texture coordinates, supports engine
    feature for high precision normals
-   LOD Support, allowing engine maximum of 8 LOD levels and full
    dithering support
-   Full NavMesh support
-   Async collision updates. As collision can be slow to update, the RMC
    can offload it from the game thread.
-   StaticMesh conversion in game and editor, as well as conversion
    to/from ProceduralMesh/DynamicMesh

::: section
# Subsections of {#subsections-of .a11y-only}

::: headline
:::

::: article-subheading
Chapter 1
:::

# Quick Start

Discover how to install and configure the RealtimeMeshComponent, as well
as differences between Core and Pro versions.

::: section
# Subsections of Quick Start {#subsections-of-quick-start .a11y-only}

::: headline
:::

# Install from Marketplace

You can purchase the RMC on the [Unreal Engine
Marketplace!](https://unrealengine.com/marketplace/product/runtime-mesh-component){.highlight
target="_blank"}

Installing the Realtime Mesh Component from the Unreal Engine
Marketplace is as simple as purchasing the plugin, and installing it to
your current engine like any other plugin.

From here if you'd like to use the RMC from C++ code, you simply need to
add
[`PublicDependencyModuleNames.Add("RealtimeMeshComponent");`{.copy-to-clipboard-code
data-code="PublicDependencyModuleNames.Add(\"RealtimeMeshComponent\");"}[]{.copy-to-clipboard-button
title="Copy to clipboard"}]{.copy-to-clipboard} to your Build.cs file
for your project module like other modules from c++.

::: headline
:::

# Install from GitHub

To install the RealtimeMeshComponent from GitHub requires a few steps.

1.  You must have a C++ ready project. If you already have C++ code,
    you're ready to go! If not you can go to
    [`Tools -> New C++ Class`{.copy-to-clipboard-code
    data-code="Tools -> New C++ Class"}[]{.copy-to-clipboard-button
    title="Copy to clipboard"}]{.copy-to-clipboard} and create some
    template actor to convert your project to a C++ project. [More Info
    can be foun
    here!](https://docs.unrealengine.com/5.0/en-US/unreal-engine-cpp-quick-start/){.highlight
    target="_blank"}

2.  You can download the plugin from GitHub depending on your version
    from one of the two links below. You can either download the project
    as a zip or clone the project by clicking the green
    [`<> Code`{.copy-to-clipboard-code
    data-code="<> Code"}[]{.copy-to-clipboard-button
    title="Copy to clipboard"}]{.copy-to-clipboard} button.

    1.  The Core version, which is free to use can be obtained here:
        1.  [Latest Stable
            Release](https://github.com/TriAxis-Games/RealtimeMeshComponent/releases){.highlight
            target="_blank"}
        2.  [Latest
            Development](git://github.com/TriAxis-Games/RealtimeMeshComponent.git){.highlight}
    2.  The Pro version, which is paid-access can be obtained here:
        1.  [Coming Soon!](#){.highlight}

3.  If you downloaded the zip version, you must unzip the contents into
    the path:
    [`{YourProject}/Plugins/RealtimeMeshComponent/`{.copy-to-clipboard-code
    data-code="{YourProject}/Plugins/RealtimeMeshComponent/"}[]{.copy-to-clipboard-button
    title="Copy to clipboard"}]{.copy-to-clipboard}

4.  Compile and run your project, which the engine should be able to do
    automatically by opening the [`.uproject`{.copy-to-clipboard-code
    data-code=".uproject"}[]{.copy-to-clipboard-button
    title="Copy to clipboard"}]{.copy-to-clipboard} file or through your
    code editor of choice.

5.  From here if you'd like to use the RMC from C++ code, you simple
    need to add [`RealtimeMeshComponent`{.copy-to-clipboard-code
    data-code="RealtimeMeshComponent"}[]{.copy-to-clipboard-button
    title="Copy to clipboard"}]{.copy-to-clipboard} to your
    [`.Build.cs`{.copy-to-clipboard-code
    data-code=".Build.cs"}[]{.copy-to-clipboard-button
    title="Copy to clipboard"}]{.copy-to-clipboard} file like any other
    module you intend to use from C++.

::: headline
:::

# Examples

The example content is now contained within the plugin itself, within
several Blueprints for different examples, or from a separate code
module in the plugin [`RealtimeMeshExamples`{.copy-to-clipboard-code
data-code="RealtimeMeshExamples"}[]{.copy-to-clipboard-button
title="Copy to clipboard"}]{.copy-to-clipboard} that serve to
demonstrate different concepts.

Make sure plugin content is visible in the content browser, if not you
can enable it through
[`Settings -> Enable Plugin Content`{.copy-to-clipboard-code
data-code="Settings -> Enable Plugin Content"}[]{.copy-to-clipboard-button
title="Copy to clipboard"}]{.copy-to-clipboard} from the content
browser.

[![Enable Plugin
Content](index.print_files/enable-plugin-content.png){style="height: auto; width: auto;"
loading="lazy"}](#image-d51dd7d2b6574cdd4d03dda6913ba67c){.lightbox-link}
[![Enable Plugin
Content](index.print_files/enable-plugin-content.png){.lightbox-image
loading="lazy"}](javascript:history.back();){#image-d51dd7d2b6574cdd4d03dda6913ba67c
.lightbox}
:::

::: headline
:::

::: article-subheading
Chapter 2
:::

# Key Concepts

## Mesh structure[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#mesh-structure"}

How are meshes structured in the RMC? From the parts of a vertex, to the
indices that turn them into triangles and the polygroups that batch
them. [Mesh
Structure](https://rmc.triaxis.games/keyconcepts/meshes/){.highlight}

## Streams[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#streams"}

Streams are the core data type of the RMC, containing one part of the
mesh data. [Learn more about
streams!](https://rmc.triaxis.games/keyconcepts/streams/){.highlight}

## Stream Builder[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#stream-builder"}

Stream builders allow for efficient, type safe access to a Stream.
[Learn more about stream
builders!](https://rmc.triaxis.games/keyconcepts/stream-builder/){.highlight}

## Stream Linkage[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#stream-linkage"}

Stream linkages are used to keep multiple streams in sync. Useful for
things like position/tangents/texcoords/vertexcolor portions of a mesh
[Learn more about stream
linkages!](https://rmc.triaxis.games/keyconcepts/stream-linkage/){.highlight}

## Stream Set[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#stream-set"}

Stream sets are the container for easily working with multiple Streams,
and StreamLinkages. [Learn more about stream
sets!](https://rmc.triaxis.games/keyconcepts/stream-set/){.highlight}

## Mesh Builder Local[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#mesh-builder-local"}

Mesh Builder Local is a helper template for working with the most common
mesh layout in an efficient, easy to use manner. [Learn more about mesh
builder
local!](https://rmc.triaxis.games/keyconcepts/mesh-local-builder/){.highlight}

::: section
# Subsections of Key Concepts {#subsections-of-key-concepts .a11y-only}

::: headline
:::

# Meshes

## Mesh Representation[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#mesh-representation"}

Mesh data in the Realtime Mesh Comonent is represented as a indexed
triangle list. With this you have two buffers:

1.  **Vertex Buffer**: Responsible for storing all the unique vertices.
    This is done ideally without duplicates, but also a single attribute
    being different causes the whole vertex to be different. The normal
    vertex attributes are:

    1.  **Position**: Represents the position of the vertex in object
        space as a three component vector for X, Y, Z
    2.  **Normal**: Represents the up-vector of the face this vertex is
        responsible for, or the common up-vector used for smooth shading
        all the adjoining triangle faces. Also known as Tangent-Z
    3.  **Tangent**: Represents the forward-vector of the face this
        vertex is responsible for, or the common forward-vector for
        smooth shading all the adjoining triangle faces. Also known as
        Tangent-X
    4.  **UV Coorinates 1-8**: Also konwn as Texture Coorinates, are the
        2D coordinats in texture space 0-1 representing the area of a
        texture to apply to this face at this vertex. There can be any
        number of these channels between 1 and 8. Can also be used to
        feed arbitrary data through to the shader and sample in Material
        as TextureCoordinate.
    5.  **Color**: Also known as Vertex Color is the color channel that
        can be used for supplying a color for this vertex, or possibly
        some other arbitrary data through to the material and read in
        the material as Vertex Color

2.  **Index Buffer**: Responsible for storing the mapping of vertices to
    triangles. Formed by a list of integers, with each group of 3
    representing a triangle, and indexing the vertices to use for that
    triangles points.

The example below shows a simple setup where we render two triangles,
using 6 entries in the index buffer, and only 4 vertices since 2
vertices are shared with two triangles each on the common edge. [![Index
Triangle
List](index.print_files/triangle-list.svg){style="height: auto; width: auto;"
loading="lazy"}](#image-4af5bbfd3e99c039b402815d1e81a8f8){.lightbox-link}
[![Index Triangle
List](index.print_files/triangle-list.svg){.lightbox-image
loading="lazy"}](javascript:history.back();){#image-4af5bbfd3e99c039b402815d1e81a8f8
.lightbox}

------------------------------------------------------------------------

## Winding Order[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#winding-order"}

One thing to be aware of and careful of is the order of your triangles
indices. This affects one standard optimization of 3d rendering called
backface culling. Unreal Engine uses Counter Clockwise Culling, so if
the triangles points are visually ordered clockwise as referenced in the
index list, it will not be rendered. This is a standard and important
optimization as it removes many triangles that cannot be visible as
they're facing away from the view.

This means when you index your vertices, you should index the points
visually counter clockwise.

------------------------------------------------------------------------

## Advanced Indexing[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#advanced-indexing"}

There are advanced things you can do with index buffers, in combinaton
with the section groups/sections of the RMC, for example:

-   You could make one large index buffer and have a separate index
    buffer for shadows to simplify the geometry used. This can be
    extremely beneficial as shadows do not care about anything except
    position, so you could combine duplicates much more aggressively
-   You could make different versions of a sections that share a common
    set of vertices and switch between which one is rendered.

------------------------------------------------------------------------

## PolyGroups[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#polygroups"}

Within the RMC, you have the option to use an extra data stream to
define the polygroup. There should be 1 entry for each Triangle in the
associated stream. This is used to easily separate the triangles into
groups which can be rendered separately. The RMC assumes the polygroups
are contiguous, so all triangles of a particular group are next to
eachother. There are utilities to sort the triangles based on the
polygroup if you want to build the triangles in an arbitrary order and
sort them later.

------------------------------------------------------------------------

## Additional Resources[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#additional-resources"}

-   More info on triangle primitives (Specifically GL_TRIANGLES):

    [https://www.khronos.org/opengl/wiki/Primitive#Triangle_primitives](https://www.khronos.org/opengl/wiki/Primitive#Triangle_primitives){.highlight
    target="_blank"}

-   You can find more info on winding order and face culling here:

    [https://www.khronos.org/opengl/wiki/Face_Culling](https://www.khronos.org/opengl/wiki/Face_Culling){.highlight
    target="_blank"}

::: headline
:::

# Streams {#streams}

Streams are the core of the data structure for the RMC. They hold a
single data type, like FVector3f or FColor, and they can handle 1-8
elements. They are meant to hold a single data component whether that's
positions, tangents, colors, texture coordinates, triangles, or
something custom. You can work directly with a stream, but it's
generally not the best way. Usually you'll want a FRealtimeMeshStreamSet

------------------------------------------------------------------------

###### Stream Key[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#stream-key"}

FRealtimeMeshStreamKey is a way of identifying the stream within things
like a StreamSet, or to the RMC itself. It contains the StreamType,
either Vertex or Index, as well as a stream name.

Some common stream keys

-   Vertex:Position - Contains the vertex position data
-   Vertex:Tangents - Contains the tangents, both TangentZ and TangentX,
    of the vertex data
-   Vertex:TexCoords - Contains the Texture Coordinates for 1-8 channels
-   Vertex::Colors - Contains the vertex color data
-   Index:Triangles - Contains the indexing data for a triangle list
-   Index:PolyGroups - Contains the poly group id for each triangle

------------------------------------------------------------------------

###### Creating a Stream[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#creating-a-stream"}

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// For example, this will create a stream with the key `Vertex:Position` and the datatype of FVector3f
auto PositionStream = FRealtimeMeshStream::Create<FVector3f>(FRealtimeMeshStreams::Position);"}
// For example, this will create a stream with the key `Vertex:Position` and the datatype of FVector3f
auto PositionStream = FRealtimeMeshStream::Create<FVector3f>(FRealtimeMeshStreams::Position);
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

------------------------------------------------------------------------

###### Bulk Adding Data[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#bulk-adding-data"}

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// If you have an array like the following (assuming it's filled with data)
TArray<FVector3f> IncomingData;

// You could add all of this at once through
PositionStream.Append(IncomingData);"}
// If you have an array like the following (assuming it's filled with data)
TArray<FVector3f> IncomingData;

// You could add all of this at once through
PositionStream.Append(IncomingData);
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

------------------------------------------------------------------------

###### Miscellaneous Helpers[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#miscellaneous-helpers"}

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// Fill the stream starting at position 10 and running for 20 elements with a default value
PositionStream.FillRange(10, 20, FVector3f(0, 0, 1));

// Zero a range of the stream starting at index 10 and running the next 20 elements
PositionStream.ZeroRange(10, 20);

// Preallocate the stream to hold 128 rows, this can cut down on reallocation and data copying.
PositionStream.Reserve(128);"}
// Fill the stream starting at position 10 and running for 20 elements with a default value
PositionStream.FillRange(10, 20, FVector3f(0, 0, 1));

// Zero a range of the stream starting at index 10 and running the next 20 elements
PositionStream.ZeroRange(10, 20);

// Preallocate the stream to hold 128 rows, this can cut down on reallocation and data copying.
PositionStream.Reserve(128);
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

::: headline
:::

# Stream Builder {#stream-builder}

Stream Builder are a helper to allow for fast interaction with Streams
of known or possibly unknown concrete data types. They allow you to
treat a stream much like an array with all the common operations like
Add/Remove/Append and indexed retrieval operators. It is possible to
also use a streambuilder to work with a subset of a stream. For example
if you wanted to work on only 1 channel of tex coords when it contains 4
total

------------------------------------------------------------------------

###### Creating a Stream Accessor[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#creating-a-stream-accessor"}

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// For example, this will create a stream with the key `Vertex:Position` and the datatype of FVector3f
auto PositionStream = FRealtimeMeshStream::Create<FVector3f>(FRealtimeMeshStreams::Position);

// This will create a simple StreamBuilder where you know the type is FVector3f. This will assert if the types do not match
// This does allow for the fastest interaction with the stream as no internal conversion is performed.
TRealtimeMeshStreamBuilder<FVector3f> PositionStreamBuilder(PositionStream);

// This will create a simple StreamBuilder where you want to work with it as if the data was FVector3d, but the actual streamdata is FVector3f.
// This will incur a slight overhead as it will do the conversion both ways internally, 
// but this is all done through templates, so it's the least overhead of the conversion available
TRealtimeMeshStreamBuilder<FVector3d, FVector3f> PositionStreamBuilder(PositionStream);

// This will create a StreamBuilder where you want to treat the data as a FVector3d, but you don't know the format of the stream. 
// This will perform a dynamic conversion internally, but the formats must be compatible. 
// In this case FVector3d, or FVector3f are safe, as well as custom data types of the same setup
TRealtimeMeshStreamBuilder<FVector3d, void> PositionStreamBuilder(PositionStream);"}
// For example, this will create a stream with the key `Vertex:Position` and the datatype of FVector3f
auto PositionStream = FRealtimeMeshStream::Create<FVector3f>(FRealtimeMeshStreams::Position);

// This will create a simple StreamBuilder where you know the type is FVector3f. This will assert if the types do not match
// This does allow for the fastest interaction with the stream as no internal conversion is performed.
TRealtimeMeshStreamBuilder<FVector3f> PositionStreamBuilder(PositionStream);

// This will create a simple StreamBuilder where you want to work with it as if the data was FVector3d, but the actual streamdata is FVector3f.
// This will incur a slight overhead as it will do the conversion both ways internally, 
// but this is all done through templates, so it's the least overhead of the conversion available
TRealtimeMeshStreamBuilder<FVector3d, FVector3f> PositionStreamBuilder(PositionStream);

// This will create a StreamBuilder where you want to treat the data as a FVector3d, but you don't know the format of the stream. 
// This will perform a dynamic conversion internally, but the formats must be compatible. 
// In this case FVector3d, or FVector3f are safe, as well as custom data types of the same setup
TRealtimeMeshStreamBuilder<FVector3d, void> PositionStreamBuilder(PositionStream);
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

------------------------------------------------------------------------

###### Creating a Strided Stream Builder[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#creating-a-strided-stream-builder"}

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// This creates a texcoords stream with 4 channels using the packed type FVector2DHalf.
auto TexCoordsStream = FRealtimeMeshStream::Create<TRealtimeMeshTexCoords<FVector2DHalf, 4>>(FRealtimeMeshStreams::TexCoords);

// We can do the same stream builder setup as above to work with all 4 channels at the same time
TRealtimeMeshStreamBuilder<TRealtimeMeshTexCoords<FVector2DHalf, 4>> TexCoordStreamBuilder(TexCoordsStream);

// Here we use the strided builder to work with channel 1 as though it was an array of FVector2f and let the builder
// perform the conversion internally.  This can be done with with all the same variations as a normal builder for no-conversion, direct-conversion, or dynamic conversion.
TRealtimeMeshStridedStreamBuilder<FVector2f, FVector2DHalf> TexCoordStreamBuilder(TexCoordsStream, 1);"}
// This creates a texcoords stream with 4 channels using the packed type FVector2DHalf.
auto TexCoordsStream = FRealtimeMeshStream::Create<TRealtimeMeshTexCoords<FVector2DHalf, 4>>(FRealtimeMeshStreams::TexCoords);

// We can do the same stream builder setup as above to work with all 4 channels at the same time
TRealtimeMeshStreamBuilder<TRealtimeMeshTexCoords<FVector2DHalf, 4>> TexCoordStreamBuilder(TexCoordsStream);

// Here we use the strided builder to work with channel 1 as though it was an array of FVector2f and let the builder
// perform the conversion internally.  This can be done with with all the same variations as a normal builder for no-conversion, direct-conversion, or dynamic conversion.
TRealtimeMeshStridedStreamBuilder<FVector2f, FVector2DHalf> TexCoordStreamBuilder(TexCoordsStream, 1);
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

------------------------------------------------------------------------

###### Working with Stream Builders[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#working-with-stream-builders"}

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// Stream Builders let you treat a stream much like a TArray
// So you have all the normal functions, plus some specialized helpers.
// Some examples are: 

// Add a element
PositionStreamBuilder.Add(FVector3f(0, 0, 0));

// Emplace an element
TexCoordsStreamBuilder.Emplace({ FVector2f(0, 0), FVector2f(0, 0) });

// Reserve the number of elements, preallocating the internal storage
PositionStreamBuilder.Reserve(100);

// Append an initializer list of 3 elements
PositionStreamBuilder.Append({ FVector3f(0, 0, 0), FVector3f(1, 1, 1), FVector3f(2, 2, 2) });

// Remove element 1
PositionStreamBuilder.RemoveAt(1);

// Set the number of elements filling any new elements with the supplied value.
PositionStreamBuilder.SetNumWithFill(128, FVector3f(0, 0, 1));"}
// Stream Builders let you treat a stream much like a TArray
// So you have all the normal functions, plus some specialized helpers.
// Some examples are: 

// Add a element
PositionStreamBuilder.Add(FVector3f(0, 0, 0));

// Emplace an element
TexCoordsStreamBuilder.Emplace({ FVector2f(0, 0), FVector2f(0, 0) });

// Reserve the number of elements, preallocating the internal storage
PositionStreamBuilder.Reserve(100);

// Append an initializer list of 3 elements
PositionStreamBuilder.Append({ FVector3f(0, 0, 0), FVector3f(1, 1, 1), FVector3f(2, 2, 2) });

// Remove element 1
PositionStreamBuilder.RemoveAt(1);

// Set the number of elements filling any new elements with the supplied value.
PositionStreamBuilder.SetNumWithFill(128, FVector3f(0, 0, 1));
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

::: headline
:::

# Stream Linkage {#stream-linkage}

Stream Linkages are used to tie multiple Streams together so that
operations that affect the size of one stream affect them all together.
This is useful to keep secondary vertex data streams the same size, and
allow you to set data only as desired.

------------------------------------------------------------------------

###### Creating a Stream Linkage[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#creating-a-stream-linkage"}

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// Create our position stream
auto PositionStream = FRealtimeMeshStream::Create<FVector3f>(FRealtimeMeshStreams::Position);

// Create our tangents stream
auto TangentsStream = FRealtimeMeshStream::Create<FRealtimeMeshTangentsNormalPrecision>(FRealtimeMeshStreams::Tangents);

// Create our linkage that we'll use to bind the vertex streams together
FRealtimeMeshStreamLinkage VerticesLinkage;

// Bind the position stream, with a deafult value of zero vector.
// We can do this simply because we know the type
VerticesLinkage.BindStream(PositionStream, FRealtimeMeshStreamDefaultRowValue::Create(FVector3f::ZeroVector));

// Bind the tangents stream, we create the default value by passing it our wanted value, and getting the final layout from the stream.
// This lets it do the conversion once and then just blit this value into the stream.
VerticesLinkage.BindStream(TangentsStream, FRealtimeMeshStreamDefaultRowValue::Create(
    FRealtimeMeshTangentsNormalPrecision(FVector3f::ZAxisVector, FVector3f::XAxisVector),
    TangentsStream.GetLayout()));

// We can setup the two builders to make working with the streams easier    
TRealtimeMeshStreamBuilder<FVector3f> PositionStreamBuilder(PositionStream);
TRealtimeMeshStreamBuilder<FRealtimeMeshTangentsHighPrecision, FRealtimeMeshTangentsNormalPrecision> TangentsStreamBuilder(TangentsStream);

// This will add the position to the position stream. It will also set the tangents stream size to the same size and default the value
// You should *NOT* use .Add on the secondary streams when setting the value, you can just used the indexed set. 
// It doesn't matter which stream you call .Add() on, it will resize them all.
int32 Index = PositionStreamBuilder.Add(FVector3f(1, 0, 0));

// Here we just set the tangent entry for this index
// If you call .Add() again then you'll end up with 2 vertices in both streams, instead of 1 vertex with the tangent set for the existing vertex
TangentsStreamBuilder[Index] = FRealtimeMeshTangentsHighPrecision(FVector3f(0, 1, 0), FVector3f(0, 0, 1));"}
// Create our position stream
auto PositionStream = FRealtimeMeshStream::Create<FVector3f>(FRealtimeMeshStreams::Position);

// Create our tangents stream
auto TangentsStream = FRealtimeMeshStream::Create<FRealtimeMeshTangentsNormalPrecision>(FRealtimeMeshStreams::Tangents);

// Create our linkage that we'll use to bind the vertex streams together
FRealtimeMeshStreamLinkage VerticesLinkage;

// Bind the position stream, with a deafult value of zero vector.
// We can do this simply because we know the type
VerticesLinkage.BindStream(PositionStream, FRealtimeMeshStreamDefaultRowValue::Create(FVector3f::ZeroVector));

// Bind the tangents stream, we create the default value by passing it our wanted value, and getting the final layout from the stream.
// This lets it do the conversion once and then just blit this value into the stream.
VerticesLinkage.BindStream(TangentsStream, FRealtimeMeshStreamDefaultRowValue::Create(
    FRealtimeMeshTangentsNormalPrecision(FVector3f::ZAxisVector, FVector3f::XAxisVector),
    TangentsStream.GetLayout()));

// We can setup the two builders to make working with the streams easier    
TRealtimeMeshStreamBuilder<FVector3f> PositionStreamBuilder(PositionStream);
TRealtimeMeshStreamBuilder<FRealtimeMeshTangentsHighPrecision, FRealtimeMeshTangentsNormalPrecision> TangentsStreamBuilder(TangentsStream);

// This will add the position to the position stream. It will also set the tangents stream size to the same size and default the value
// You should *NOT* use .Add on the secondary streams when setting the value, you can just used the indexed set. 
// It doesn't matter which stream you call .Add() on, it will resize them all.
int32 Index = PositionStreamBuilder.Add(FVector3f(1, 0, 0));

// Here we just set the tangent entry for this index
// If you call .Add() again then you'll end up with 2 vertices in both streams, instead of 1 vertex with the tangent set for the existing vertex
TangentsStreamBuilder[Index] = FRealtimeMeshTangentsHighPrecision(FVector3f(0, 1, 0), FVector3f(0, 0, 1));
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

::: headline
:::

# Stream Set {#stream-set}

StreamSets are a container holding 1 or more Streams, and potentially
some binding logic to tie streams together. At their core they function
like a hashtable containing streams referenced by the StreamKey which is
a name and buffer usage type (Vertex, Index). This is the most common
way to pass data around, and can be used to store arbitrarily complex
sets of data.

------------------------------------------------------------------------

###### Creating a basic stream set[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#creating-a-basic-stream-set"}

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// First we create the empty stream set
FRealtimeMeshStreamSet StreamSet;

// Then we can add whatever streams to it we want
FRealtimeMeshStream& PositionStream = StreamSet.AddStream<FVector3f>(FRealtimeMeshStreams::Position);
FRealtimeMeshStream& TangentsStream = StreamSet.AddStream<FRealtimeMeshTangentsNormalPrecision>(FRealtimeMeshStreams::Tangents);

// We can choose to add the streams to a link pool if we want to
StreamSet.AddStreamToLinkPool(\"Vertices\", FRealtimeMeshStreams::Position, FRealtimeMeshStreamDefaultRowValue::Create(FVector3f::ZeroVector));
StreamSet.AddStreamToLinkPool(\"Vertices\", FRealtimeMeshStreams::Tangents, FRealtimeMeshStreamDefaultRowValue::Create(
    FRealtimeMeshTangentsNormalPrecision(FVector3f::ZAxisVector, FVector3f::XAxisVector),
    TangentsStream.GetLayout()));

// Then we can bind the stream builds to the contained streams like a individual stream
TRealtimeMeshStreamBuilder<FVector3f> PositionStreamBuilder(StreamSet.FindChecked(FRealtimeMeshStreams::Position));"}
// First we create the empty stream set
FRealtimeMeshStreamSet StreamSet;

// Then we can add whatever streams to it we want
FRealtimeMeshStream& PositionStream = StreamSet.AddStream<FVector3f>(FRealtimeMeshStreams::Position);
FRealtimeMeshStream& TangentsStream = StreamSet.AddStream<FRealtimeMeshTangentsNormalPrecision>(FRealtimeMeshStreams::Tangents);

// We can choose to add the streams to a link pool if we want to
StreamSet.AddStreamToLinkPool("Vertices", FRealtimeMeshStreams::Position, FRealtimeMeshStreamDefaultRowValue::Create(FVector3f::ZeroVector));
StreamSet.AddStreamToLinkPool("Vertices", FRealtimeMeshStreams::Tangents, FRealtimeMeshStreamDefaultRowValue::Create(
    FRealtimeMeshTangentsNormalPrecision(FVector3f::ZAxisVector, FVector3f::XAxisVector),
    TangentsStream.GetLayout()));

// Then we can bind the stream builds to the contained streams like a individual stream
TRealtimeMeshStreamBuilder<FVector3f> PositionStreamBuilder(StreamSet.FindChecked(FRealtimeMeshStreams::Position));
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

------------------------------------------------------------------------

###### Copying stream sets[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#copying-stream-sets"}

Stream Sets by default don't allow simple copy through assignment. This
is because this can be a heavy operation to copy all the stream data. If
you actually want to copy a stream you should use the explicit copy
constructor

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// Use the explicit copy constructor as implicit copy and assignment are disallowed for performance reasons.
FRealtimeMeshStreamSet StreamSet2(StreamSet);"}
// Use the explicit copy constructor as implicit copy and assignment are disallowed for performance reasons.
FRealtimeMeshStreamSet StreamSet2(StreamSet);
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

You can move to stream set around from one storage to another. This is
far more efficient as it doesn't duplicate the internal data but instead
moves it from one owner to the next. This will reset the source
streamset in the process

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// Move assignment is fast because it does not duplicate the data and instead moves the ownership of it.
FRealtimeMeshStreamSet StreamSet2 = MoveTemp(StreamSet);"}
// Move assignment is fast because it does not duplicate the data and instead moves the ownership of it.
FRealtimeMeshStreamSet StreamSet2 = MoveTemp(StreamSet);
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::

::: headline
:::

# Mesh Builder Local {#mesh-builder-local}

Mesh Builder Local is a helper utility that makes building and working
with the mesh data for the most common vertex format simpler. You can
customize the exact data types and it will internally switch between the
necessary conversion types just like TRealtimeMeshStreamBuilder.

------------------------------------------------------------------------

###### Creating a basic mesh builder local[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#creating-a-basic-mesh-builder-local"}

::: {.wrap-code .highlight}
``` {.pre-code tabindex="0" style="color:#f8f8f2;background-color:#272822;-moz-tab-size:4;-o-tab-size:4;tab-size:4;" data-code="// First we create the empty stream set
FRealtimeMeshStreamSet StreamSet;

// Then we can bind the Mesh Builder Local to it.
// We can configure the stream data types through the template parameters
// We can also supply void to get dynamic conversion for unknown stream layouts.
TRealtimeMeshBuilderLocal<uint16, FPackedNormal, FVector2DHalf, 1> Builder(StreamSet);

// We can decide what vertex elements are enabled.
Builder.EnableTangents();
Builder.EnableTexCoords();
Builder.EnableColors();
Builder.EnablePolyGroups();

// We can add a vertex, and optionally set things like the tangents, color, texcoords.
// We can then get the new index from it to use later.
int32 V0 = Builder.AddVertex(FVector3f(-50.0f, 0.0f, 0.0f))
    .SetNormalAndTangent(FVector3f(0.0f, -1.0f, 1.0f), FVector3f(1.0f, 0.0f, 0.0f))
    .SetColor(FColor::Red)
    .SetTexCoord(FVector2f(0.0f, 0.0f));

// Now we can add a triangle giving it the indices of the vertices for the 3 corners, as well as optionally supplying the polygroup
Builder.AddTriangle(V0, V1, V2, 0);"}
// First we create the empty stream set
FRealtimeMeshStreamSet StreamSet;

// Then we can bind the Mesh Builder Local to it.
// We can configure the stream data types through the template parameters
// We can also supply void to get dynamic conversion for unknown stream layouts.
TRealtimeMeshBuilderLocal<uint16, FPackedNormal, FVector2DHalf, 1> Builder(StreamSet);

// We can decide what vertex elements are enabled.
Builder.EnableTangents();
Builder.EnableTexCoords();
Builder.EnableColors();
Builder.EnablePolyGroups();

// We can add a vertex, and optionally set things like the tangents, color, texcoords.
// We can then get the new index from it to use later.
int32 V0 = Builder.AddVertex(FVector3f(-50.0f, 0.0f, 0.0f))
    .SetNormalAndTangent(FVector3f(0.0f, -1.0f, 1.0f), FVector3f(1.0f, 0.0f, 0.0f))
    .SetColor(FColor::Red)
    .SetTexCoord(FVector2f(0.0f, 0.0f));

// Now we can add a triangle giving it the indices of the vertices for the 3 corners, as well as optionally supplying the polygroup
Builder.AddTriangle(V0, V1, V2, 0);
```

[]{.copy-to-clipboard-button title="Copy to clipboard"}
:::
:::

::: headline
:::

::: article-subheading
Chapter 3
:::

# Component Core

Learn the core concepts of the Realtime Mesh Component and how to use
it!

::: section
# Subsections of Component Core {#subsections-of-component-core .a11y-only}

::: headline
:::

# Structure

There are several core pieces of the Realtime Mesh Component, that all
work together.

## URealtimeMeshActor[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#urealtimemeshactor"}

The RealtimeMeshActor is much like the StaticMeshActor. It provides and
easy base class for an RMC powered actor and provides some helper logic
to not regenerate on construction script in editor when doing things
like dragging the actor around. It doesn't provide much more logic on
its own and you can ignore it and create the RealtimeMeshComponent
directly on your own actor types, or even multiple RMCs on your own
actor type.

## URealtimeMeshComponent[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#urealtimemeshcomponent"}

The RealtimeMeshComponent is the minimum usable component, and provides
similar functionality to how StaticMeshComponent works where you can
place one or more of them on an actor.

## URealtimeMesh[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#urealtimemesh"}

The RealtimeMesh object holds the underlying data, manages the collision
structurs, and common data, and allows sharing a RealtimeMesh with
multiple RealtimeMeshComponents and thereby multiple RealtimeMeshActors
or custom actors. There are a couple versions of a RealtimeMesh
including RealtimeMeshSimple, RealtimeMeshDynamic, and
RealtimeMeshComposable. You can also make your own versions of
RealtimeMesh or derivatives of any of the other concrete
implementations.

1.  RealtimeMeshSimple - Provides a similar feel to ProceduralMesh, and
    older style Realtime/RuntimeMeshComponents where you can feed it
    data and forget about it.
2.  RealtimeMeshDynamic(Pro) - Provides a way to feed a DynamicMesh into
    the RealtimeMesh to get the benefits of GeometryScripting in editor
    and Runtime, with the performance/feature benefits of RealtimeMesh
3.  RealtimeMeshComposable(Pro) - Provides a way to work with a
    hierarchical factory structure to generate one or more meshes.

### FRealtimeMesh[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemesh"}

FRealtimeMesh is the central piece that communicates with the rendering
thread, and defines an interface that must be implemented by the
concrete tyeps which provides the rendering and collision data when
necessary.

### FRealtimeMeshMaterialSlot[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemeshmaterialslot"}

Much like a material slot in a static mesh, defines the separate
materials in use by the realtime mesh. A renderable section uses the
index of the slot to choose the material it will ultimately use.

### FRealtimeMeshLOD[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemeshlod"}

Starting the hierarchy of mesh data, it encapsulates an abritrary number
of section groups/sections to render when the LOD is desired.

The configuration of a LOD contains the ScreenSize to activate this LOD,
if a higher level LOD isn't active.

LODs are completely independent, and can contain different numbers and
configurations of section groups and sections. With this, the more
detailed LODs could contain more independent sections to provide more
detail and use simpler combined sections in lower LODs for performance.

### FRealtimeMeshSectionGroup[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemeshsectiongroup"}

The middle of the hierarchy of mesh data, it contains a set of vertex
and index buffers that can be shared by an abitrary number of sections
within the group.

Allows for re-using the buffer between sections, as well as minimizing
state changes when rendering sections.

### FRealtimeMeshSection[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemeshsection"}

The lowest level of th hierarchy of mesh data. It links a section of
vertex/index buffers in the parent section group, with a material, and
rendering settings to draw a portion of the mesh within the active LOD.

Sections can be used to render portions of the parent groups buffers in
differnt ways. You could use this to render a different region of the
index buffer for shadows using a subset of vertices, and another section
to render the full index buffer for the more detailed visible mesh.

The Stream Range of a section is the start/end element of the vertices
and indices in the parent section groups buffers.

The Section Config allows setting multiple configuration options for the
section including whether the section is visible, or casts a shadow, as
well as the material slot. It also allows setting the draw type from one
of the two following options:

1.  Static - Meant for sections that don't update frequently, so it
    prioritizes rendering performance at the cost of slower updates
2.  Dynamic - Meant for sections that update frequently even up to
    frame-by-frame, so it prioritizes update latency over rendering
    performance.

### FRealtimeMeshProxy[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemeshproxy"}

The render thread counterpart to FRealtimeMesh, owns all the RHI data
and works with the renderer to render the Realtime Mesh.

This is done, like other render proxy objects in the engine, to own a
separate state on the render thread so not to have lock contention with
the game thread.

### FRealtimeMeshLODProxy[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemeshlodproxy"}

The render thread counterpart to FRealtimeMeshLOD, owns all the render
thread section groups and sections used to render the Realtime Mesh

### FRealtimeMeshSectionGroupProxy[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemeshsectiongroupproxy"}

The render thread counterpart to FRealtimeMeshSectionGroup, owns all the
render thread sections used to render the RealtimeMesh. Also owns the
actual RHI buffers, vertex factories, and ray tracing data.

### FRealtimeMeshSectionProxy[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemeshsectionproxy"}

The render thread counterpart to FRealtimeMeshSection, owns all the
render thread configuration for the section, clones the configuration
from the parent Section to allow the render thread to facilitate the
renderer in setting up mesh drawing.

### FRealtimeMeshComponentProxy[]{.anchor title="Copy link to clipboard" clipboard-text="https://rmc.triaxis.games/index.print#frealtimemeshcomponentproxy"}

The render thread counterpart to URealtimeMeshComponent, owns the render
thread state of a URealtimeMeshComponent, including its linkage to the
underlying FRealtimeMeshProxy to facilitate the component rendering.
:::
:::
:::
:::
:::
