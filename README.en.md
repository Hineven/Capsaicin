# MIGI
Mixed Implicit caching for Global Illumination (MIGI) is a real-time global illumination technique that combines the benefits of octahedral probe encoding and parametric functions in real-time dynamic radiance caching.

It is built on Capsaicin and runs on a screen space probe hierarchy nearly identical to Lumen. It uses the secondary hash grid radiance cache from GI1.1 for simplicity.

The novel thing about this technique is that the conventional octahedral probe encoding is enhanced with a parametric function that allows for a more efficient and accurate representation of the radiance field. 

The parametric function is a sum of a series of spherical gaussians, which co-operates with octahedral probes and is optimized / reprojected / filtered in real-time. 

Compared with conventional caching schemes in real-time rendering, the crucial benifit of using spherical gaussians is that it enables "all frequency caching". That is, there is no band limitation about the frequency of the incident radiance distribution to cache.

The followings shows difference between SG enhanced caching and conventional octahedral probe caching.

This is SG enhanced caching with a moving light.
<img src="migi_docs/moving-light-SG.gif" height="400px" width="600px">SG enhanced caching</img>

This is conventional octahedral probe caching. It misses the glossy reflections, is darker and more noisy even with static lighting.
<img src="migi_docs/no-SG.png" height="400px" width="600px">Conventional</img>

**Note:** we have not implemented any specific direct light algorithm yet. There are no light samples so current algorithm just process all emissive objects as indirect lit.
So just regard the above emitter an indirect light source. It makes little sense comparing the current render quality with algorithms optimized for direct lighting with light priors and light samples.

The current implementation is yet to be completed. It runs at above 130 fps at 1080p on a RTX 3090, and is scalable for higher quality.

![Bedroom](migi_docs/track1.gif)
![Sponza](migi_docs/track2.gif)

There're several unresolved things in the current implementation due to lack of time.
1. Multiple spherical gaussian optimization. All my previous attempts failed and I decided to put it aside for a moment.
2. Current SG re-projection method is not that stable (it produces visually perceptible noise).
3. Spatial filtering of SGs is yet to be explored and implemented
4. Multiple bugs in the current implementation. I've worked on them for weeks and still discovering & resolving them.

Also, there're some other benefits of using spherical gaussians.
The numerical approximation about integrating SG light sources with Torrance-Sparrow BSDFs (GGX) exists and is performant;
It's possible to scale the number of SGs per probe when the complexity of incident lighting distribution is varying;
Collecting statistics about the geometries in the directions of each SG may be useful for explicit guidance on fast update with changing lighting.

However they are just my future directions to explore for now...