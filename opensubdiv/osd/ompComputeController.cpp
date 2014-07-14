//
//   Copyright 2013 Pixar
//
//   Licensed under the Apache License, Version 2.0 (the "Apache License")
//   with the following modification; you may not use this file except in
//   compliance with the Apache License and the following modification to it:
//   Section 6. Trademarks. is deleted and replaced with:
//
//   6. Trademarks. This License does not grant permission to use the trade
//      names, trademarks, service marks, or product names of the Licensor
//      and its affiliates, except as required to comply with Section 4(c) of
//      the License and to reproduce the content of the NOTICE file.
//
//   You may obtain a copy of the Apache License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the Apache License with the above modification is
//   distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the Apache License for the specific
//   language governing permissions and limitations under the Apache License.
//

#include "../far/stencilTables.h"
#include "../osd/ompComputeController.h"
#include "../osd/ompKernel.h"

#include <cassert>

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {


OsdOmpComputeController::OsdOmpComputeController(int numThreads) {

    _numThreads = (numThreads == -1) ? omp_get_max_threads() : numThreads;
}

void
OsdOmpComputeController::ApplyStencilTableKernel(
    FarKernelBatch const &batch, ComputeContext const *context) const {

    assert(context);

    FarStencilTables const * vertexStencilTables =
        context->GetVertexStencilTables();

    if (vertexStencilTables) {

        // We assume that the control vertices are packed at the beginning of
        // the vertex buffer (hence the single descriptor)

        float * destBuffer = _currentBindState.vertexBuffer + 
            vertexStencilTables->GetNumControlVertices() * _currentBindState.vertexDesc.stride;


        OsdOmpComputeStencils(_currentBindState.vertexDesc,
                              _currentBindState.vertexBuffer,
                              destBuffer,
                              &vertexStencilTables->GetSizes().at(0),
                              &vertexStencilTables->GetOffsets().at(0),
                              &vertexStencilTables->GetControlIndices().at(0),
                              &vertexStencilTables->GetWeights().at(0),
                              batch.start,
                              batch.end);
    }
}

void
OsdOmpComputeController::Synchronize() {
    // XXX:
}

}  // end namespace OPENSUBDIV_VERSION
}  // end namespace OpenSubdiv
