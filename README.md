# rttypes

This is just an experiment (hence the single main.cpp).

I want to define components in a scripting language of a game engine, but store them efficiently engine-side (in C++). Therefore I need to construct, destruct and access types defined at runtime. I need generic types (for things such as vector - dynamic arrays) and structs (product types). This is an attempt at implementing such a thing.

This code is kind of scary and I am not sure if anyone (including me) should use it.

Also obviously a bunch of stuff is missing. The VectorData class is not quite complete, const overloads are missing for pretty much everything and I should probably have a way to define a custom allocator (likely pmr) for VectorData and maybe string too.
