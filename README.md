# RenderToy

RenderToy is tool for quickly prototyping GPU-based rendering pipelines and shaders. I'm mostly making it for myself as a replacement for ATi's RenderMonkey.

As of v0.1 compute-based render passes can be chained together via a node graph to form an image processing pipeline. RenderToy currently uses OpenGL 4, with plans to move to Vulkan at some point. 

There is no built-in text editor. Instead, RenderToy is meant to be used side-by-side with the text editor of your choice. A plugin for Sublime Text 3 is provided which provides in-line reporting of shader build errors.

The current version is Windows-only. The code is *mostly* OS-agnostic, so porting should not be too difficult.

![rendertoy_v0 1](https://cloud.githubusercontent.com/assets/16522064/22633580/0ef6bb42-ec23-11e6-8dfd-86c9e6236b28.png)

