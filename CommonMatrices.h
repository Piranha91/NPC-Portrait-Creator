#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <NifFile.hpp>
#include <Nodes.hpp>

namespace Matrices {

    /**
     * @brief Converts a row-major nifly::MatTransform (Z-up) to a column-major glm::mat4.
     *
     * This function handles the crucial transpose operation required when converting from the
     * row-major memory layout used by nifly to the column-major layout expected by GLM and OpenGL.
     *
     * @param niflyMat The input matrix from the nifly library.
     * @return The equivalent column-major matrix in GLM format.
     */
    inline glm::mat4 NiflyToGlm(const nifly::MatTransform& niflyMat) {
        // nifly's ToMatrix() returns a temporary Matrix4 object.
        // To safely get a pointer to its data, we must copy it to a local variable.
        nifly::Matrix4 tempMat = niflyMat.ToMatrix();
        // glm::make_mat4 reads from the pointer and glm::transpose performs the final conversion.
        return glm::transpose(glm::make_mat4(&tempMat[0]));
    }


    /**
     * @brief An invariant matrix that handles the crucial coordinate system conversion from NIF to the renderer's world space.
     *
     * This matrix transforms vertices and vectors from the NIF file's root coordinate system
     * to the renderer's world coordinate system.
     *
     * NIF Standard:      +X is right, +Y is forward, +Z is up.
     * Renderer Standard:   +X is right, +Y is up,     +Z is backward.
     *
     * Transformation implemented by this matrix:
     * - NIF's +X (right) becomes Renderer's -X (left), causing a reflection.
     * - NIF's +Y (forward) becomes Renderer's +Z (backward).
     * - NIF's +Z (up) becomes Renderer's +Y (up).
     *
     * Input Space: NIF Root Space (Z-up)
     * Output Space: Renderer's World Space (Y-up)
     */
    const static glm::mat4 NIF_ROOT_TO_WORLD_YUP = glm::mat4(
        -1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

} // namespace Matrices