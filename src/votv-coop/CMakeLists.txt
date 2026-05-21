# votv-coop — UE4SS C++ mod (placeholder)
#
# Builds as a DLL loaded by UE4SS into Voices of the Void (UE4.27).
# This is a skeleton: the UE4SS C++ mod SDK (from reference/RE-UE4SS/)
# is wired in during Phase 2, when the first hook lands. No sources are
# listed yet by design (methodology 0.1.8: first commit is project shape,
# not the first hook).

cmake_minimum_required(VERSION 3.20)
project(votv-coop CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Principle 7 subtrees. Sources added in Phase 2.
#   src/ue_wrap/*.cpp  — engine-wrapper layer (no network/gameplay state)
#   src/coop/*.cpp     — gameplay/network layer
#
# add_library(votv-coop SHARED
#     src/ue_wrap/...
#     src/coop/...
# )
# target_include_directories(votv-coop PRIVATE include)
# target_link_libraries(votv-coop PRIVATE UE4SS)   # from reference/RE-UE4SS

message(STATUS "votv-coop: skeleton CMakeLists — no targets yet (Phase 0).")
