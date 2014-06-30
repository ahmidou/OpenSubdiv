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
#include "../far/patchTables.h"
#include "../far/patchTablesFactory.h"
#include "../far/refineTables.h"
#include "../vtr/level.h"
#include "../vtr/refinement.h"

#include <cstring>


namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

//
//  In development -- creation from FarRefineTables rather than HbrMesh.
//      As with uniform patches, the method is static because it requires no cached state.
//
namespace {
    //
    //  Reimplementing the tagging outside Hbr now -- some of these enums should probably
    //  be defined more as part of FarPatchTables...
    //
    //  Like the HbrFace<T>::AdaptiveFlags, this struct aggregates all of the face tags
    //  supporting feature adaptive refinement.  For now it is not used elsewhere and can
    //  remain local to this implementation, but we may want to move it into a header of
    //  its own if it has greater use later.
    //
    //  Note that several properties being assigned here attempt to do so given a 4-bit
    //  mask of properties at the edges or vertices of the quad.  Still not sure exactly
    //  what will be done this way, but the goal is to create lookup tables (of size 16
    //  for the 4 bits) to quickly determine was is needed, rather than iteration and
    //  branching on the edges or vertices.
    //
    struct PatchFaceTag {
    public:
        //  The HBR_ADAPTIVE TransitionType from <hbr/face.h> -- now named to more clearly
        //  reflect the number and orientation of transitional edges.  Note that the values
        //  assigned here need to match the intended purpose to remain consistent with Hbr:
        enum TransitionType {
            NONE          = 0,
            TRANS_ONE     = 1,
            TRANS_TWO_ADJ = 2,
            TRANS_THREE   = 3,
            TRANS_ALL     = 4,
            TRANS_TWO_OPP = 5
        };

    public:
        unsigned int   _hasPatch        : 1;
        unsigned int   _isRegular       : 1;
        unsigned int   _isTransitional  : 1;
        unsigned int   _transitionType  : 3;
        unsigned int   _transitionRot   : 2;
        unsigned int   _boundaryIndex   : 2;
        unsigned int   _boundaryCount   : 3;
        unsigned int   _hasBoundaryEdge : 3;

        void clear() { std::memset(this, 0, sizeof(*this)); }

        void assignBoundaryPropertiesFromEdgeMask(int boundaryEdgeMask) {
            //
            //  The number of rotations to apply for boundary or corner patches varies on both
            //  where the boundary/corner occurs and whether boundary or corner -- so using a
            //  4-bit mask should be sufficient to quickly determine all cases:
            //
            //  Note that we currently expect patches with multiple boundaries to have already
            //  been isolated, so asserts are applied for such unexpected cases.
            //
            //  Is the compiler going to build the 16-entry lookup table here, or should we do
            //  it ourselves?
            //
            _hasBoundaryEdge = true;

            switch (boundaryEdgeMask) {
            case 0x0:  _boundaryCount = 0, _boundaryIndex = 0, _hasBoundaryEdge = false;  break;  // no boundaries

            case 0x1:  _boundaryCount = 1, _boundaryIndex = 0;  break;  // boundary edge 0
            case 0x2:  _boundaryCount = 1, _boundaryIndex = 1;  break;  // boundary edge 1
            case 0x3:  _boundaryCount = 2, _boundaryIndex = 1;  break;  // corner/crease vertex 1
            case 0x4:  _boundaryCount = 1, _boundaryIndex = 2;  break;  // boundary edge 2
            case 0x5:  assert(false);                           break;  // N/A - opposite boundary edges
            case 0x6:  _boundaryCount = 2, _boundaryIndex = 2;  break;  // corner/crease vertex 2
            case 0x7:  assert(false);                           break;  // N/A - three boundary edges
            case 0x8:  _boundaryCount = 1, _boundaryIndex = 3;  break;  // boundary edge 3
            case 0x9:  _boundaryCount = 2, _boundaryIndex = 0;  break;  // corner/crease vertex 0
            case 0xa:  assert(false);                           break;  // N/A - opposite boundary edges
            case 0xb:  assert(false);                           break;  // N/A - three boundary edges
            case 0xc:  _boundaryCount = 2, _boundaryIndex = 3;  break;  // corner/crease vertex 3
            case 0xd:  assert(false);                           break;  // N/A - three boundary edges
            case 0xe:  assert(false);                           break;  // N/A - three boundary edges
            case 0xf:  assert(false);                           break;  // N/A - all boundaries
            default:   assert(false);                           break;
            }
        }

        void assignBoundaryPropertiesFromVertexMask(int boundaryVertexMask) {
            //
            //  This is strictly needed for the irregular case when a vertex is a boundary in
            //  the presence of no boundary edges -- an extra-ordinary face with only one corner
            //  on the boundary.
            //
            //  Its unclear at this point if patches with more than one such vertex are supported
            //  (if so, how do we deal with rotations) so for now we only allow one such vertex
            //  and assert for all other cases.
            //
            assert(_hasBoundaryEdge == false);

            switch (boundaryVertexMask) {
            case 0x0:  _boundaryCount = 0;                      break;  // no boundaries
            case 0x1:  _boundaryCount = 1, _boundaryIndex = 0;  break;  // boundary vertex 0
            case 0x2:  _boundaryCount = 1, _boundaryIndex = 1;  break;  // boundary vertex 1
            case 0x3:  assert(false);                           break;
            case 0x4:  _boundaryCount = 1, _boundaryIndex = 2;  break;  // boundary vertex 2
            case 0x5:  assert(false);                           break;
            case 0x6:  assert(false);                           break;
            case 0x7:  assert(false);                           break;
            case 0x8:  _boundaryCount = 1, _boundaryIndex = 3;  break;  // boundary vertex 3
            case 0x9:  assert(false);                           break;
            case 0xa:  assert(false);                           break;
            case 0xb:  assert(false);                           break;
            case 0xc:  assert(false);                           break;
            case 0xd:  assert(false);                           break;
            case 0xe:  assert(false);                           break;
            case 0xf:  assert(false);                           break;
            default:   assert(false);                           break;
            }
        }

        void assignTransitionRotationForCorner(int transitionEdgeMask) {
            //
            //  Corner transition patches have only two interior edges that may be transitional.
            //
            //  Either both are transitional (TRANS_TWO_ADJ) with only a single possible orientation,
            //  or only one is transitional (TRANS_ONE) with two possibilities.  The former case is
            //  trivial.  For the latter, use the known corner index to identify one of the two
            //  possible transition masks and test to determine between the two cases.
            //
            if (_transitionType == TRANS_ONE) {
                int const edgeMaskPerCorner[] = { 4, 8, 1, 2 };

                _transitionRot = 1 + (edgeMaskPerCorner[_boundaryIndex] != transitionEdgeMask);
            } else {
                _transitionRot = 1;
            }
        }

        void assignTransitionRotationForBoundary(int transitionEdgeMask) {
            //
            //  Boundary transition patches have three interior edges that may be transitional.
            //
            //  The case of all three transitional (TRANS_THREE) has only one orientation, while the
            //  case of two opposite transitional edges (TRANS_TWO_OPP) also has only one orientation.
            //  So both of these are trivially handled.
            //
            //  The case of a single transitional edge (TRANS_ONE) or one transitional edge (TRANS_TWO_ADJ)
            //  both have multiple orientations -- three for TRANS_ONE and two for TRANS_TWO_ADJ.  Each is
            //  handled separately:
            //
            if (_transitionType == TRANS_ONE) {
                if (transitionEdgeMask == (1 << ((_boundaryIndex + 2) % 4))) {
                    _transitionRot = 2;
                } else if (transitionEdgeMask == (1 << ((_boundaryIndex + 1) % 4))) {
                    _transitionRot = 1;
                } else {
                    _transitionRot = 3;
                }
            } else if (_transitionType == TRANS_TWO_ADJ) {
                int const edgeMaskPerBoundary[] = { 6, 12, 9, 3 };

                _transitionRot = 1 + (edgeMaskPerBoundary[_boundaryIndex] == transitionEdgeMask);
            } else {
                _transitionRot = 1;
            }
        }

        void assignTransitionPropertiesFromEdgeMask(int transitionEdgeMask) {
            //
            //  Note the transition rotations will be a function of the boundary rotations, and
            //  so boundary rotations/index should have been previously assigned:
            //
            //  As with the boundary rotation case, consider retrieving values from static 16-
            //  entry lookup tables if possible (depending on the function involving boundary
            //  rotations)...
            //
            _isTransitional = (transitionEdgeMask != 0);

            switch (transitionEdgeMask) {
            case 0x0:  _transitionType = NONE;           break;  // no transitions
            case 0x1:  _transitionType = TRANS_ONE;      break;  // single edge 0
            case 0x2:  _transitionType = TRANS_ONE;      break;  // single edge 1
            case 0x3:  _transitionType = TRANS_TWO_ADJ;  break;  // two adjacent edges, 0 and 1
            case 0x4:  _transitionType = TRANS_ONE;      break;  // single edge 2
            case 0x5:  _transitionType = TRANS_TWO_OPP;  break;  // two opposite edges, 0 and 2
            case 0x6:  _transitionType = TRANS_TWO_ADJ;  break;  // two adjacent edges, 1 and 2
            case 0x7:  _transitionType = TRANS_THREE;    break;  // three edges, all but 3
            case 0x8:  _transitionType = TRANS_ONE;      break;  // single edge 3
            case 0x9:  _transitionType = TRANS_TWO_ADJ;  break;  // two adjacent edges, 3 and 0
            case 0xa:  _transitionType = TRANS_TWO_OPP;  break;  // two opposite edges, 1 and 3
            case 0xb:  _transitionType = TRANS_THREE;    break;  // three edges, all but 2
            case 0xc:  _transitionType = TRANS_TWO_ADJ;  break;  // two adjacent edges, 2 and 3
            case 0xd:  _transitionType = TRANS_THREE;    break;  // three edges, all but 1
            case 0xe:  _transitionType = TRANS_THREE;    break;  // three edges, all but 0
            case 0xf:  _transitionType = TRANS_ALL;      break;  // all edges
            default:   assert(false);                    break;
            }

            //  May need another switch/lookup table here or combine it with the above -- the
            //  results below are a function of both transition and boundary properties...
            if (transitionEdgeMask == 0) {
                _transitionRot = 0;
            } else if (_boundaryCount == 0) {
                _transitionRot = _boundaryIndex;
            } else if (_boundaryCount == 1) {
                assignTransitionRotationForBoundary(transitionEdgeMask);
            } else if (_boundaryCount == 2) {
                assignTransitionRotationForCorner(transitionEdgeMask);
            }
        }
    };

    inline void
    offsetAndPermuteIndices(unsigned int const indices[], unsigned int count,
                            unsigned int offset, unsigned int const permutation[],
                            unsigned int result[])
    {
        if (permutation) {
            for (unsigned int i = 0; i < count; ++i) {
                result[i] = offset + indices[permutation[i]];
            }
        } else if (offset) {
            for (unsigned int i = 0; i < count; ++i) {
                result[i] = offset + indices[i];
            }
        } else {
            std::memcpy(result, indices, count * sizeof(unsigned int));
        }
    }

    //
    //  Move the following Vtr related searches into static methods later -- once we are
    //  free of the current <T> parameter to the factory, which forces the implementations
    //  into the header file...
    //
    void
    vtrGetQuadOffsets(VtrLevel const& level, VtrIndex fIndex, unsigned int offsets[])
    {
        VtrIndexArray fVerts = level.getFaceVertices(fIndex);

        for (int i = 0; i < 4; ++i) {
            VtrIndex      vIndex = fVerts[i];
            VtrIndexArray vFaces = level.getVertexFaces(vIndex);

            int thisFaceInVFaces = -1;
            for (int j = 0; j < vFaces.size(); ++j) {
                if (fIndex == vFaces[j]) {
                    thisFaceInVFaces = j;
                    break;
                }
            }
            assert(thisFaceInVFaces != -1);

            unsigned int vOffsets[2];
            vOffsets[0] = thisFaceInVFaces;
            vOffsets[1] = thisFaceInVFaces + 1;
            if (vOffsets[1] == vFaces.size()) {
                vOffsets[0] = 0;
                vOffsets[1] = vFaces.size() - 1;
            }
            offsets[i] = vOffsets[0] | (vOffsets[1] << 8);
        }
    }

    FarPatchParam *
    vtrComputePatchParam(FarRefineTables const & refTables,
                         int levelIndex, VtrIndex faceIndex, int rotation,
                         FarPatchParam *coord)
    {
        if (coord == NULL) return NULL;

        //  Parameters to be assigned:
        unsigned short u = 0;
        unsigned short v = 0;
        unsigned char  rots = rotation;
        unsigned char  depth = levelIndex;
        bool           nonquad = (refTables.GetFaceVertices(levelIndex, faceIndex).size() != 4);

        // Move up the hierarchy accumulating u,v indices to the coarse level:
        unsigned short ofs = 1;

        for (int i = levelIndex; i > 0; --i) {
            VtrRefinement const& refinement  = refTables._refinements[i-1];
            VtrLevel const&      parentLevel = refTables._levels[i-1];

            VtrIndex parentFaceIndex    = refinement.getChildFaceParentFace(faceIndex);
            int      childIndexInParent = refinement.getChildFaceInParentFace(faceIndex);

            if (parentLevel.getFaceVertices(parentFaceIndex).size() == 4) {
                switch ( childIndexInParent ) {
                    case 0 :                     break;
                    case 1 : { u+=ofs;         } break;
                    case 2 : { u+=ofs; v+=ofs; } break;
                    case 3 : {         v+=ofs; } break;
                }
                ofs = (unsigned short)(ofs << 1);
            } else {
                nonquad = true;
            }
            faceIndex = parentFaceIndex;
        }

        //if (refTables.HasPtex()) {
        //    faceIndex = refTables.GetPtexIndex(faceIndex);
        //}
        coord->Set(faceIndex, u, v, rots, depth, nonquad);

        return ++coord;
    }

} // namespace anon


template <>
FarPatchTables *
FarPatchTablesFactory<int>::Create( FarRefineTables const * refineTables, int fvarwidth )
{
    //  This function should probably handle both uniform and sparsely refined tables at some
    //  point, but for now we are focusing on feature adaptive (i.e. sparse) needs.
    assert(!refineTables->IsUniform());

    //
    //  Iterate through the levels of refinement to inspect and tag components with information
    //  relative to patch generation.  We allocate all of the tags locally and use them to
    //  populate the patches once a complete inventory has been taken and all tables appropriately
    //  allocated and initialized:
    //
    //  The first Level may have no Refinement if it is the only level -- similarly the last Level
    //  has no Refinement, so a single level is effectively the last, but with less information
    //  available in some cases, as it was not generated by refinement.
    //
    Counter patchInventory;

    std::vector<PatchFaceTag> allPatchTags(refineTables->GetNumFacesTotal());

    PatchFaceTag * levelPatchTags = &allPatchTags[0];

    for (int i = 0; i < (int)refineTables->_levels.size(); ++i) {
        VtrLevel const * level = &refineTables->_levels[i];

        //
        //  Given components at Level[i], we need to be looking at Refinement[i] -- and not
        //  [i-1] -- because the Refinement has transitional information for its parent edges
        //  and faces.  But we also need to be looking at Refinement[i-1] to know about the
        //  ancestry of the components, i.e. are they "complete" wrt their ancestors (if not,
        //  they are supporting components
        //
        //  For components in this level, we want to determine:
        //    - what Edges are "transitional" (already done in Refinement for parent)
        //    - what Faces are "transitional" (already done in Refinement for parent)
        //    - what Faces are "complete" (done for child vertices in Refinement)
        //
        bool isLevelFirst = (i == 0);
        bool isLevelLast  = (i == (refineTables->_levels.size() - 1));

        VtrRefinement const * refinePrev = isLevelFirst ? 0 : &refineTables->_refinements[i-1];
        VtrRefinement const * refineNext = isLevelLast  ? 0 : &refineTables->_refinements[i];

        VtrRefinement::SparseTag const * vtrFaceTags = refineNext ? &refineNext->_parentFaceTag[0] : 0; 

        for (int faceIndex = 0; faceIndex < level->getNumFaces(); ++faceIndex) {
            VtrRefinement::SparseTag vtrFaceTag = vtrFaceTags ? vtrFaceTags[faceIndex] : VtrRefinement::SparseTag();
            PatchFaceTag&            patchTag   = levelPatchTags[faceIndex];

            patchTag.clear();
            patchTag._hasPatch = false;

            //
            //  This face does not warrant a patch under the following conditions:
            //
            //      - the face was fully refined into child faces
            //      - the face is not a quad (should have been refined, so assert)
            //      - the face is not "complete"
            //
            //  The first is trivially determined, and the second is really redundant.  The
            //  last -- "incompleteness" -- indicates a face that exists to support the limit
            //  of some neighboring component, and which does not have its own neighborhood
            //  fully defined for its limit.  If any child vertex of a vertex of this face is
            //  "incomplete", the face must be "incomplete" (note all faces in level 0 are
            //  complete and do not warrant closer inspection).
            //
            if (vtrFaceTag._selected) {
                continue;
            }

            VtrIndexArray const& fVerts = level->getFaceVertices(faceIndex);
            assert(fVerts.size() == 4);

            if (!isLevelFirst && (refinePrev->_childVertexTag[fVerts[0]]._incomplete ||
                                  refinePrev->_childVertexTag[fVerts[1]]._incomplete ||
                                  refinePrev->_childVertexTag[fVerts[2]]._incomplete ||
                                  refinePrev->_childVertexTag[fVerts[3]]._incomplete)) {
                continue;
            }

            //
            //  We have a quad that will be represented as a B-spline or Gregory patch.  Use
            //  the "composite" tag for the face that combines tags for all face-verts -- we
            //  can use it to quickly determine if any vertex is irregular or on a boundary.
            //
            //  Inspect the edges for boundaries and transitional edges and pack results into
            //  4-bit masks.  We detect boundary edges rather than vertices as we hope to
            //  replace the mask in future with one for infinitely sharp edges -- allowing
            //  us to detect regular patches and avoid isolation.  We still need to account
            //  for the irregular/xordinary case when a corner vertex is a boundary but there
            //  are no boundary edges.
            //
            //  As for transition detection, assign the transition properties (even if 0) as
            //  their rotations override boundary rotations (when no transition)
            //
            //  NOTE on non-manifold support:
            //      Patches from non-manifold verts are not yet supported -- the extraction
            //  of patch points at corners currently assumes manifold.  Supporting interior
            //  hard edges (below) will allow non-manifold patches with inf sharp boundaries.
            //
            //  NOTE on infinitely sharp (hard) edges:
            //      We should be able to adapt this later to detect hard (inf-sharp) edges
            //  rather than just boundary edges -- there is a similar tag per edge.  That
            //  should allow us to generate regular patches for interior hard features.
            //
            VtrLevel::VTag compFaceVertTag = level->getFaceCompositeVTag(fVerts);

            //  Patches for non-manifold faces not yet supported (see above note)
            assert(!compFaceVertTag._nonManifold);

            patchTag._hasPatch  = true;
            patchTag._isRegular = !compFaceVertTag._xordinary;

            bool hasBoundaryVertex = compFaceVertTag._boundary;
            if (hasBoundaryVertex) {
                VtrIndexArray const& fEdges = level->getFaceEdges(faceIndex);

                int boundaryEdgeMask = ((level->_edgeTags[fEdges[0]]._boundary) << 0) |
                                       ((level->_edgeTags[fEdges[1]]._boundary) << 1) |
                                       ((level->_edgeTags[fEdges[2]]._boundary) << 2) |
                                       ((level->_edgeTags[fEdges[3]]._boundary) << 3);

                if (boundaryEdgeMask) {
                    patchTag.assignBoundaryPropertiesFromEdgeMask(boundaryEdgeMask);
                } else {
                    int boundaryVertMask = ((level->_vertTags[fVerts[0]]._boundary) << 0) |
                                           ((level->_vertTags[fVerts[1]]._boundary) << 1) |
                                           ((level->_vertTags[fVerts[2]]._boundary) << 2) |
                                           ((level->_vertTags[fVerts[3]]._boundary) << 3);

                    patchTag.assignBoundaryPropertiesFromVertexMask(boundaryVertMask);
                }
            }
            patchTag.assignTransitionPropertiesFromEdgeMask(vtrFaceTag._transitional);

            //
            //  Identify and increment counts for regular patches (both non-transitional and
            //  transitional) and extra-ordinary patches (always non-transitional):
            //
            if (patchTag._isRegular) {
                int transIndex = patchTag._transitionType;
                int transRot   = patchTag._transitionRot;

                if (patchTag._boundaryCount == 0) {
                    patchInventory.R[transIndex]++;
                } else if (patchTag._boundaryCount == 1) {
                    patchInventory.B[transIndex][transRot]++;
                } else {
                    patchInventory.C[transIndex][transRot]++;
                }
            } else {
                if (patchTag._boundaryCount == 0) {
                    patchInventory.G++;
                } else {
                    patchInventory.GB++;
                }
            }
        }
        levelPatchTags += level->getNumFaces();
    }

    //
    //  Now create the instance of the tables, allocate and initialize its members based on the
    //  patch inventory determined, and traverse the face list to construct the patches for each:
    //

    //  We should have been accumulating these above...
    int maxValence   = 32;
    int numPtexFaces = refineTables->_levels[0].getNumFaces();

    FarPatchTables * tables = new FarPatchTables(maxValence);

    // Populate the patch array descriptors
    FarPatchTables::PatchArrayVector & parray = tables->_patchArrays;
    parray.reserve( patchInventory.getNumPatchArrays() );

    int voffset=0, poffset=0, qoffset=0;

    for (Descriptor::iterator it=Descriptor::begin(Descriptor::FEATURE_ADAPTIVE_CATMARK);
            it!=Descriptor::end(); ++it) {
        pushPatchArray( *it, parray, patchInventory.getValue(*it), &voffset, &poffset, &qoffset );
    }

    tables->_fvarData._fvarWidth = fvarwidth;
    tables->_numPtexFaces = numPtexFaces;

    // Allocate various tables
    allocateTables( tables, 0, fvarwidth );

    // Specifics for Gregory patches -- the vertex valence table is initialized for many vertices
    // not incident gregory patches, so we'll use a local vector of flags to mark only those that
    // are needed and avoid initializing all others:
    std::vector<unsigned char> gregoryVertexFlags;

    bool hasGregoryPatches = (patchInventory.G > 0) || (patchInventory.GB > 0);
    if (hasGregoryPatches) {
        tables->_quadOffsetTable.resize( patchInventory.G*4 + patchInventory.GB*4 );

        gregoryVertexFlags.resize(refineTables->GetNumVerticesTotal(), false);
    }


    //  Everything from the construction of the FarPatchTables instance to here could be bundled
    //  into a single static initializeTables() method -- provided we set other simple members
    //  first (maxValence, numPtexFaces, fvarwidth, etc.) it should only need the patch counters.


    //  Initialize some local variables to simplify access to the patch data that needs to be
    //  populated as we iterate through the faces.

    // Setup convenience pointers at the beginning of each patch array for each
    // table (patches, ptex, fvar)
    CVPointers    iptrs;
    ParamPointers pptrs;
    FVarPointers  fptrs;

    for (Descriptor::iterator it=Descriptor::begin(Descriptor::FEATURE_ADAPTIVE_CATMARK); it!=Descriptor::end(); ++it) {
        FarPatchTables::PatchArray * pa = tables->findPatchArray(*it);

        if (not pa) continue;

        iptrs.getValue( *it ) = &tables->_patches[pa->GetVertIndex()];
        pptrs.getValue( *it ) = &tables->_paramTable[pa->GetPatchIndex()];

        if (fvarwidth>0)
            fptrs.getValue( *it ) = &tables->_fvarData._data[pa->GetPatchIndex() * 4 * fvarwidth];
    }

    FarPatchTables::QuadOffsetTable::value_type *quad_G_C0_P = patchInventory.G>0 ? &tables->_quadOffsetTable[0] : 0;
    FarPatchTables::QuadOffsetTable::value_type *quad_G_C1_P = patchInventory.GB>0 ? &tables->_quadOffsetTable[patchInventory.G*4] : 0;

    //
    //  Now iterate through the faces for all levels and populate the patches:
    //
    //  Regarding globally "remapping" the indices -- I prefer to defer remapping them all until
    //  one pass at the end, so that we can more easily make it optional.  We do still need to
    //  offset components within levels for patch table entries -- so we need a "level offset"
    //  for the vertices that we gather.
    //
    int levelFaceOffset = 0;
    int levelVertOffset = 0;

    for (int i = 0; i < (int)refineTables->_levels.size(); ++i) {
        VtrLevel const * level = &refineTables->_levels[i];

        levelPatchTags = &allPatchTags[levelFaceOffset];

        for (int faceIndex = 0; faceIndex < level->getNumFaces(); ++faceIndex) {
            PatchFaceTag& patchTag = levelPatchTags[faceIndex];

            if (!patchTag._hasPatch) continue;

            if (patchTag._isRegular) {
                unsigned int   patchVerts[16];

                int tIndex = patchTag._transitionType;
                int rIndex = patchTag._transitionRot;
                int bIndex = patchTag._boundaryIndex;

                if (patchTag._boundaryCount == 0) {
                    unsigned int const * permuteInterior = 0;

                    level->gatherQuadRegularInteriorPatchVertices(faceIndex, patchVerts);
                    offsetAndPermuteIndices(patchVerts, 16, levelVertOffset, permuteInterior, iptrs.R[tIndex]);

                    iptrs.R[tIndex] += 16;
                    pptrs.R[tIndex] = vtrComputePatchParam(*refineTables, i, faceIndex, bIndex, pptrs.R[tIndex]);
                    // fptrs.R[tIndex] += fvarwidth * 4;
                } else {
                    //  For the boundary and corner cases, the Hbr code makes some adjustments to the
                    //  rotations here from the way they were defined earlier.  That raises questions
                    //  as to the purpose of the earlier assignments and their naming.  I'd prefer to
                    //  label the sets of rotations for their intended purpose, and to compute and
                    //  assign them earlier for use here with no adjustment.
                    //
                    //  Non-transition case:
                    //      rot = 0;  // outside switch
                    //      f->_adaptiveFlags.brots = (f->_adaptiveFlags.rots + 1) % 4;
                    //  Transition case:
                    //      rot = f->_adaptiveFlags.brots;  //  is this now same as transition rots?
                    //
                    //  Both cases of "rot" above are now handled with the "transition rotation" -- still
                    //  not clear what the purpose of the other is.  Need to look into usage of these
                    //  adaptive-flag rotations in:
                    //      getOneRing, computePatchParam, computeFVarData
                    //  It may be that a separate "face rotation" flag is warranted if we need something
                    //  else dependent on the boundary orientation.
                    //
                    if (patchTag._boundaryCount == 1) {
                        unsigned int const * permuteBoundary = 0;

                        level->gatherQuadRegularBoundaryPatchVertices(faceIndex, patchVerts, bIndex);
                        offsetAndPermuteIndices(patchVerts, 12, levelVertOffset, permuteBoundary, iptrs.B[tIndex][rIndex]);

                        iptrs.B[tIndex][rIndex] += 12;
                        pptrs.B[tIndex][rIndex] = vtrComputePatchParam(*refineTables, i, faceIndex, bIndex, pptrs.B[tIndex][rIndex]);
                        // fptrs.B[tIndex][rIndex] = computeFVarData(...,   fptrs.B[tIndex][rIndex], true);
                    } else {
                        unsigned int const * permuteCorner = 0;

                        int paramRots = (bIndex + 3) % 4;

                        level->gatherQuadRegularCornerPatchVertices(faceIndex, patchVerts, bIndex);
                        offsetAndPermuteIndices(patchVerts, 9, levelVertOffset, permuteCorner, iptrs.C[tIndex][rIndex]);

                        iptrs.C[tIndex][rIndex] += 9;
                        pptrs.C[tIndex][rIndex] = vtrComputePatchParam(*refineTables, i, faceIndex, paramRots, pptrs.C[tIndex][rIndex]);
                        // fptrs.C[tIndex][rIndex] = computeFVarData(...,   fptrs.C[tIndex][rIndex], true);
                    }
                }
            } else {
                if (patchTag._boundaryCount == 0) {
                    // Gregory Regular Patch (4 CVs + quad-offsets / valence tables)
                    VtrIndexArray const faceVerts = level->getFaceVertices(faceIndex);
                    for (int j = 0; j < 4; ++j) {
                        iptrs.G[j] = faceVerts[j] + levelVertOffset;
                        gregoryVertexFlags[iptrs.G[j]] = true;
                    }
                    iptrs.G += 4;

                    vtrGetQuadOffsets(*level, faceIndex, quad_G_C0_P);
                    quad_G_C0_P += 4;

                    pptrs.G = vtrComputePatchParam(*refineTables, i, faceIndex, 0, pptrs.G);
                    // fptrs.G = computeFVarData(f, fvarwidth, fptrs.G, true);
                } else {
                    // Gregory Boundary Patch (4 CVs + quad-offsets / valence tables)
                    VtrIndexArray const faceVerts = level->getFaceVertices(faceIndex);
                    for (int j = 0; j < 4; ++j) {
                        iptrs.GB[j] = faceVerts[j] + levelVertOffset;
                        gregoryVertexFlags[iptrs.GB[j]] = true;
                    }
                    iptrs.GB += 4;

                    vtrGetQuadOffsets(*level, faceIndex, quad_G_C1_P);
                    quad_G_C1_P += 4;

                    pptrs.GB = vtrComputePatchParam(*refineTables, i, faceIndex, patchTag._boundaryIndex, pptrs.GB);
                    // fptrs.GB = computeFVarData(f, fvarwidth, fptrs.GB, true);
                }
            }
        }
        levelFaceOffset += level->getNumFaces();
        levelVertOffset += level->getNumVertices();
    }

    //
    //  Now deal with the "vertex valence" table for Gregory patches -- this table contains the one-ring
    //  of vertices around each vertex.  Currently it is extremely wasteful for the following reasons:
    //      - it allocates 2*maxvalence+1 for ALL vertices
    //      - it initializes the one-ring for ALL vertices
    //  We use the full size expected, but we avoiding initializing the vast majority of vertices that
    //  are not associated with gregory patches by having previously marked those that are above.
    //
    if (hasGregoryPatches) {
        const int SizePerVertex = 2*maxValence + 1;

        FarPatchTables::VertexValenceTable & vTable = tables->_vertexValenceTable;
        vTable.resize(refineTables->GetNumVerticesTotal() * SizePerVertex);

        int vOffset = 0;
        int levelLast = refineTables->_levels.size() - 1;
        for (int i = 0; i <= levelLast; ++i) {
            VtrLevel const * level = &refineTables->_levels[i];

            int vTableOffset = vOffset * SizePerVertex;

            if (i < levelLast) {
                for (int vIndex = 0; vIndex < level->getNumVertices(); ++vIndex) {
                    vTable[vTableOffset] = 0;
                    vTableOffset += SizePerVertex;
                }
            } else {
                for (int vIndex = 0; vIndex < level->getNumVertices(); ++vIndex) {
                    int* vTableEntry = &vTable[vTableOffset];

                    //
                    //  If not marked as a vertex of a gregory patch, just set to 0 to ignore.  Otherwise
                    //  gather the one-ring around the vertex and set its resulting size (note the negative
                    //  size used to distinguish between boundary/interior):
                    //
                    if (!gregoryVertexFlags[vIndex + vOffset]) {
                        vTableEntry[0] = 0;
                    } else {
                        int* ringDest = vTableEntry + 1;
                        int  ringSize = level->gatherManifoldVertexRingFromIncidentQuads(vIndex, ringDest);

                        //  Determine boundary/interior from size of the ring returned:
                        if (ringSize & 1) {
                            //  May need to permute the boundary order to match...
                            vTableEntry[0] = -ringSize;
                        } else {
                            vTableEntry[0] = ringSize;
                        }
                    }
                    vTableOffset += SizePerVertex;
                }
            }
            vOffset += level->getNumVertices();
        }
    }

    return tables;
}

} // end namespace OPENSUBDIV_VERSION
} // end namespace OpenSubdiv