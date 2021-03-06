#include "OpenGLDrv.h"
#include "gl_frontend.h"
#include "gl_backend.h"
#include "gl_sky.h"
#include "gl_buffers.h"
#include "gl_math.h"
#include "protocol.h"		//!! for RF_XXX consts only (SHELL_*, TRANSLUCENT, BBOX, DEPTHHACK, WEAPONMODEL)!


namespace OpenGLDrv {

static int visFrame, drawFrame;
static int currentEntity;
static int viewCluster;

viewPortal_t vp;

// map zones
byte		zoneMask[MAX_MAP_ZONES/8];
bool		forceVisMap;

// entities
refEntity_t gl_entities[MAX_GLENTITIES];
int			gl_numEntities;

// dlights
#define MAX_GLDLIGHTS		(MAX_DLIGHTS * 4)		// reserve more for frames with different dlight sets
refDlight_t	gl_dlights[MAX_GLDLIGHTS];
int			gl_numDlights;


GLenum		gl_fogMode;
float		gl_fogColor[4];
float		gl_fogDensity;
float		gl_fogStart, gl_fogEnd;

#define BACKFACE_EPSILON	1						// Q2: 0.01, Q3: 8


/* Culling on BSP:
 *   1. PVS
 *   2. frustum cull leafs
 *   3. backface/frustum cull leaffaces
 * Culling on inline models:
 *   1. frustum cull (entire model)
 *   2. occlusion cull (entire model)
 *   3. backface/frustum cull model surfaces
 * Culling triangle models:
 *   1. frustum cull (entire model)
 *   2. occlusion cull (entire model)
 */

/*------------------- Frustum culling -------------------*/

#define FRUSTUM_INSIDE			1
#define FRUSTUM_OUTSIDE			2
#define FRUSTUM_CENTER_OUTSIDE	4
#define FRUSTUM_ON				(FRUSTUM_INSIDE|FRUSTUM_OUTSIDE)


static int Cull(CBox &b, int frustumMask)
{
	if (!gl_frustumCull->integer)
		return FRUSTUM_ON;

	if (!frustumMask)
		return FRUSTUM_INSIDE;

	int res = FRUSTUM_INSIDE;
	int	i;
	CPlane *pl;
	for (i = 0, pl = vp.frustum; i < NUM_FRUSTUM_PLANES; i++, pl++)
		if (frustumMask & (1 << i))
		{
			int tmp = b.OnPlaneSide(*pl);
			if (tmp == 2) return FRUSTUM_OUTSIDE;
			if (tmp == 3) res = FRUSTUM_ON;
		}
	return res;
}


// Uses entity to determine transformations
static int TransformedCull(CBox &b, refEntity_t *e)
{
	if (!gl_frustumCull->integer)
		return FRUSTUM_ON;

	int frustumMask = e->frustumMask;	//?? get this as OR(all_occupied_leafs->frustumMask) or remove
	if (!frustumMask)
		return FRUSTUM_INSIDE;

#if 1	// this version FASTER
	int res = 0;
	int	i, mask;
	CPlane *fr;
	for (i = 0, mask = 1, fr = vp.frustum; i < NUM_FRUSTUM_PLANES; i++, mask<<=1, fr++)
	{
		if (!(mask & frustumMask))
		{
			res |= FRUSTUM_INSIDE;
			continue;
		}

		// transform frustum plane to entity coordinate system
		CPlane pl;
		e->coord.TransformPlane(*fr, pl);

		switch (b.OnPlaneSide(pl))	// do not use BOX_ON_PLANE_SIDE -- useless (non-axial planes)
		{
		case 1:
			frustumMask &= ~mask;
			res |= FRUSTUM_INSIDE;
			break;
		case 2:
			return FRUSTUM_OUTSIDE;
		default:
			res = FRUSTUM_ON;
		}
	}
#else
	// transform bounding box to a world coordinates
	CVec3	box[8];
	CVec3	*v = box;
	int i;
	for (i = 0; i < 8; i++, v++)	// check all 8 verts of bounding box
	{
		CVec3 tmp;
		b.GetVertex(i, tmp);
		e->coord.UnTransformPoint(tmp, *v);
	}

	// perform frustum culling
	int res = 0;
	int mask;
	CPlane *fr;
	for (i = 0, mask = 1, fr = vp.frustum; i < NUM_FRUSTUM_PLANES; i++, mask<<=1, fr++)
	{	// loop by frustum planes
		int		j;

		if (!(mask & frustumMask))
		{
			res |= FRUSTUM_INSIDE;
			continue;
		}

		int side = 0;
		for (j = 0, v = box; j < 8; j++, v++)
		{	// loop by box verts
			if (dot(*v, fr->normal) >= fr->dist)
				side |= FRUSTUM_INSIDE;
			else
				side |= FRUSTUM_OUTSIDE;
		}

		if (side == FRUSTUM_OUTSIDE)
			return FRUSTUM_OUTSIDE;	// the box is completely outside this frustum plane
		if (side == FRUSTUM_INSIDE)
			frustumMask &= ~mask;	// the box is completely inside this frustum plane
		res |= side;
	}
#endif

	if (frustumMask & ~MAX_FRUSTUM_MASK)	// update frustum mask for entity
		e->frustumMask = frustumMask & MAX_FRUSTUM_MASK;

	return res;
}


static int SphereCull(const CVec3 &origin, float radius, byte *frustumMask)
{
	if (!gl_frustumCull->integer)
	{
		if (frustumMask)
			*frustumMask = MAX_FRUSTUM_MASK;
		return FRUSTUM_ON;
	}

	// perform frustum culling
	int ret = 0;
	int mask = frustumMask ? *frustumMask : MAX_FRUSTUM_MASK;
	int	i, m;
	const CPlane *pl;
	for (i = 0, pl = vp.frustum, m = 1; i < NUM_FRUSTUM_PLANES; i++, pl++, m <<= 1)
	{	// loop by frustum planes
		if (!(m & mask)) continue;

		float dist = dot(origin, pl->normal) - pl->dist;
		if (dist < -radius)
			return FRUSTUM_OUTSIDE;
		if (dist <= radius)
		{
			ret |= FRUSTUM_OUTSIDE;
			if (dist < 0) ret |= FRUSTUM_CENTER_OUTSIDE;
		}
		else
			mask &= ~m;
	}
	if (frustumMask)
		*frustumMask = mask;
	return ret | FRUSTUM_INSIDE;
}


// Reduced SphereCull()
static int PointCull(const CVec3 &point, int frustumMask)
{
	if (!gl_frustumCull->integer)
		return FRUSTUM_INSIDE;

	if (!frustumMask)
		return FRUSTUM_INSIDE;

	int	i;
	const CPlane *pl;
	for (i = 0, pl = vp.frustum; i < NUM_FRUSTUM_PLANES; i++, pl++)
		if (frustumMask & (1 << i))
		{
			if (dot(point, pl->normal) - pl->dist < 0)
				return FRUSTUM_OUTSIDE;
		}
	return FRUSTUM_INSIDE;
}


#define NUM_TEST_BRUSHES	2

static bool BoxOccluded(const refEntity_t *e, const CVec3 &size2)
{
	float	mins2[2], maxs2[2];
	CVec3	v, left, right;
	int		n, brushes[NUM_TEST_BRUSHES];

	STAT(clock(gl_stats.occlTest));

	// check: when one of entity bbox dimensions comparable with a distance to object,
	// we should not perform occlusion test (otherwise, we can get clipped BoxRect, which
	// will be rotated around entity center, and non-clipped part of rect will be inside
	// world object, and entity will be mistakenly occluded)
	float dist = e->dist2 / 2;
	if (e->size2[0] > dist || e->size2[1] > dist || e->size2[2] > dist) goto notOccluded;

	// project box to plane
	if (!GetBoxRect(e, size2, mins2, maxs2))
	{
notOccluded:	// we use "goto" for unclock() + return false ...
		STAT(unclock(gl_stats.occlTest));
		return false;
	}

	// top-left
	VectorMA(e->center, mins2[0], vp.view.axis[1], left);
	VectorMA(left, mins2[1], vp.view.axis[2], v);
#if 0
	static cvar_t *test=NULL;
	EXEC_ONCE(test = Cvar_Get("test","32"));
	n = CM_BrushTrace(vp.view.origin, v, brushes, test->integer);
#else
	n = CM_BrushTrace(vp.view.origin, v, brushes, NUM_TEST_BRUSHES);
#endif
	if (!n) goto notOccluded;

	// bottom-right (diagonal with 1st point)
	VectorMA(e->center, maxs2[0], vp.view.axis[1], right);
	VectorMA(right, maxs2[1], vp.view.axis[2], v);
	n = CM_RefineBrushTrace(vp.view.origin, v, brushes, n);
	if (!n) goto notOccluded;

	// bottom-left
	VectorMA(left, maxs2[1], vp.view.axis[2], v);
	n = CM_RefineBrushTrace(vp.view.origin, v, brushes, n);
	if (!n) goto notOccluded;

	// top-right
	VectorMA(right, mins2[1], vp.view.axis[2], v);
	n = CM_RefineBrushTrace(vp.view.origin, v, brushes, n);
	if (!n) goto notOccluded;

	STAT(unclock(gl_stats.occlTest));
	return true;
}


static bool WorldBoxOccluded(const CBox &bounds)
{
//	static cvar_t *test;
//	if (!test) test=Cvar_Get("test","32");
	// optimize !!: 8 -> 4 points (find contour -- fast for axial boxes); change trace order
	// in a case of fast non-occluded test: top-left, bottom-right, other 2 points
	STAT(clock(gl_stats.occlTest));

	int	n, brushes[NUM_TEST_BRUSHES];	// keep data between iterations
	for (int i = 0; i < 8; i++)
	{
		CVec3	v;
		bounds.GetVertex(i, v);

		if (i == 0)
			n = CM_BrushTrace(vp.view.origin, v, brushes, NUM_TEST_BRUSHES);	// test->integer);
		else
			n = CM_RefineBrushTrace(vp.view.origin, v, brushes, n);
		if (!n)
		{
			STAT(unclock(gl_stats.occlTest));
			return false;
		}
	}

	STAT(unclock(gl_stats.occlTest));

	return true;
}


static void SetupModelMatrix(refEntity_t *e)
{
	float	matrix[4][4];
	int		i, j, k;

	//?? skip if ent->worldModel == true
	/* Matrix contents:
	 *  a00   a10   a20     x
	 *  a01   a11   a21     y
	 *  a02   a12   a22     z
	 *    0     0     0     1
	 */
	memset(matrix, 0, sizeof(matrix));
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			matrix[i][j] = e->coord.axis[i][j];

	if (e->mirror)
	{
		// negate left axis (left<->right)
		FNegate(matrix[1][0]);
		FNegate(matrix[1][1]);
		FNegate(matrix[1][2]);
	}

	matrix[3][0] = e->coord.origin[0];
	matrix[3][1] = e->coord.origin[1];
	matrix[3][2] = e->coord.origin[2];
	matrix[3][3] = 1;
	// e.modelMatrix = world.modelMatrix * matrix
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
		{
			float s = 0;
			for (k = 0; k < 4; k++)
				s += vp.modelMatrix[k][j] * matrix[i][k];
			e->modelMatrix[i][j] = s;
		}
	// set e.modelvieworg
	// NOTE: in Q3 axis may be non-normalized, so, there result is divided by length(axis[i])
	e->coord.TransformPoint(vp.view.origin, e->modelvieworg);
}


// Point trace to visible INLINE models; function based on CL_ClipMoveToEntities()
// NOTE: can easily extend to any (invisible too) inline models (add flag "visibleOnly")
static void ClipTraceToEntities(trace_t &tr, const CVec3 &start, const CVec3 &end, int brushmask)
{
	CVec3	traceDir;
	VectorSubtract(end, start, traceDir);
	float traceLen = traceDir.NormalizeFast();

	refEntity_t *e = gl_entities + vp.firstEntity;
	for (int i = 0; i < vp.numEntities; i++, e++)
	{
		CVec3	tmp, center2;

		if (!e->visible || !e->model || e->model->type != MODEL_INLINE)
			continue;

		inlineModel_t *im = static_cast<inlineModel_t*>(e->model);
		VectorSubtract(e->center, start, center2);

		// collision detection: line vs sphere
		float entPos = dot(center2, traceDir);
		if (entPos < -im->radius || entPos > traceLen + im->radius)
			continue;		// too near / too far

		VectorMA(center2, -entPos, traceDir, tmp);
		float dist2 = dot(tmp, tmp);
		if (dist2 >= im->radius * im->radius) continue;

		// trace
		trace_t	trace;
		if (!e->worldMatrix)
			CM_TransformedBoxTrace(trace, start, end, nullBox, im->headnode, brushmask, e->coord.origin, e->coord.axis);
		else
			CM_BoxTrace(trace, start, end, nullBox, im->headnode, brushmask);

		if (trace.allsolid || trace.startsolid || trace.fraction < tr.fraction)
		{
			trace.ent = (edict_s *)e;
		 	if (tr.startsolid)
			{
				tr = trace;
				tr.startsolid = true;
			}
			else
				tr = trace;
		}
		else if (trace.startsolid)
			tr.startsolid = true;
		if (tr.allsolid) return;
	}
}


// returns false when cylinder is outside frustum; modifies v1 and v2
static bool CutCylinder(CVec3 &v1, CVec3 &v2, float radius)
{
	int		i;
	CPlane *pl;
	for (i = 0, pl = vp.frustum; i < NUM_FRUSTUM_PLANES; i++, pl++)
	{	// loop by frustum planes
		float	frac;
		CVec3	delta;

		float dist1 = dot(v1, pl->normal) - pl->dist;				// pl->DistanceTo() w/o checking pl->type
		float dist2 = dot(v2, pl->normal) - pl->dist;
		if (dist1 < -radius && dist2 < -radius) return false;		// cylinder is outside the view frustum

		if (dist1 < -radius)
		{	// modify v1 so dist1 == -radius
			VectorSubtract(v1, v2, delta);			// v2 + delta = v1
//			frac = (dist2 - radius) / (dist2 - dist1);	-- test
			frac = (dist2 + radius) / (dist2 - dist1);
			VectorMA(v2, frac, delta, v1);
		}
		else if (dist2 < -radius)
		{	// modify v2 so dist2 == -radius
			VectorSubtract(v2, v1, delta);			// v1 + delta = v2
//			frac = (dist1 - radius) / (dist1 - dist2);	-- test
			frac = (dist1 + radius) / (dist1 - dist2);
			VectorMA(v1, frac, delta, v2);
		}
		// else - completely inside this plane
	}
	return true;
}


/*-------------- BSP object insertion ----------------*/

/* NOTE:
  - required for ents with shader->sort > OPAQUE (sort alpha-ents)
  - useful for other entity types if world occlusion culling will be
    imlemented (insert ents AFTER world culling) - ??
*/

// Find nearest to sphere center visible leaf, occupied by entity bounding sphere
static CBspLeaf *SphereLeaf(const CVec3 &origin, float radius)
{
	int		sptr = 0;
	CBspNode *stack[MAX_TREE_DEPTH];

	CBspNode *node = bspfile.nodes;
	while (true)
	{
		if (node->isLeaf)
		{
			CBspLeaf *leaf = (CBspLeaf*)node;
			if (leaf->ex->frame == drawFrame)
				return leaf;			// visible => mision complete

			if (!sptr)
				return NULL;	// whole tree visited, but not leaf found (may happens when !gl_frustumCull & ent outside frustum)

			node = stack[--sptr];
			continue;
		}

		if (node->ex->visFrame != visFrame)
		{
			if (!sptr) return NULL;
			node = stack[--sptr];
			continue;
		}

		float dist = node->plane->DistanceTo(origin);
		if (dist > radius)
			node = node->children[0];	// side 1
		else if (dist < -radius)		// use mradius = -radius for speedup ??
			node = node->children[1];	// side 2
		else
		{
			// both sides -- go origin's side first
			// (what this will do: if localOrigin point is visible, return PointLeaf(org))
			if (!IsNegative(dist))
			{
				stack[sptr++] = node->children[1];
				node = node->children[0];
			}
			else
			{
				stack[sptr++] = node->children[0];
				node = node->children[1];
			}
		}
	}
}


// Find nearest (by draw order) visible leaf, occupied by entity sphere,
// or nearest to a sphere center leaf with alpha surfaces (if one)
static CBspLeaf *AlphaSphereLeaf(const CVec3 &origin, float radius)
{
	int sptr = 0;
	CBspNode *stack[MAX_TREE_DEPTH];

	CBspNode *node = bspfile.nodes;
	int drawOrder = 0;
	CBspLeaf *drawLeaf = NULL;
	while (true)
	{
		if (node->isLeaf)
		{
			CBspLeaf *leaf = (CBspLeaf*)node;
			if (leaf->ex->frame == drawFrame)
			{
				if (leaf->ex->haveAlpha)
					return leaf;		// visible & alpha => mision complete
				else if (leaf->ex->drawOrder > drawOrder)
				{
					drawOrder = leaf->ex->drawOrder;
					drawLeaf  = leaf;
				}
			}

			if (!sptr)
				return drawLeaf;		// whole tree visited, but no leafs with alpha found

			node = stack[--sptr];
			continue;
		}

		// process node
		if (node->ex->visFrame != visFrame || (!node->ex->haveAlpha && node->ex->drawOrder < drawOrder))
		{
			if (!sptr) return drawLeaf;
			node = stack[--sptr];
			continue;
		}

		float dist = node->plane->DistanceTo(origin);
		if (dist > radius)
			node = node->children[0];	// side 1
		else if (dist < -radius)
			node = node->children[1];	// side 2
		else
		{
			// both sides -- go origin's side first
			// (what this will do: if localOrigin point is visible, return PointLeaf(org))
			if (!IsNegative(dist))
			{
				stack[sptr++] = node->children[1];
				node = node->children[0];
			}
			else
			{
				stack[sptr++] = node->children[0];
				node = node->children[1];
			}
		}
	}
}


// Find nearest (by draw order) visible leaf, occupied by beam
static CBspLeaf *BeamLeaf(const CVec3 &v1, const CVec3 &v2)
{
	int sptr = 0;
	struct {
		CBspNode *node;
		CVec3	v1, v2;
	} stack[MAX_TREE_DEPTH], *st;

	CBspNode *node = bspfile.nodes;
	int drawOrder = 0;
	CBspLeaf *drawLeaf = NULL;
	CVec3 v1a = v1;
	CVec3 v2a = v2;

#define PUSH_NODE(n, start, end) \
	st = &stack[sptr++]; \
	st->node = n;		\
	st->v1   = start;	\
	st->v2   = end;

#define POP_NODE()		\
	if (!sptr)			\
		node = NULL;	\
	else				\
	{					\
		st = &stack[--sptr]; \
		node = st->node; \
		v1a  = st->v1;	\
		v2a  = st->v2;	\
	}

	while (node)
	{
		if (node->ex->visFrame != visFrame)
		{
			POP_NODE();
			continue;
		}
		if (node->isLeaf)
		{
			CBspLeaf *leaf = (CBspLeaf*)node;
			if (leaf->ex->frame != drawFrame)
			{
				POP_NODE();
				continue;
			}

			if (leaf->ex->drawOrder > drawOrder)
			{
				drawOrder = leaf->ex->drawOrder;
				drawLeaf  = leaf;
			}
			POP_NODE();
			continue;
		}

		// node
		float t1 = node->plane->DistanceTo(v1a);
		float t2 = node->plane->DistanceTo(v2a);
		int s1 = IsNegative(t1);
		int s2 = IsNegative(t2);
		if (!(s1 | s2))
			node = node->children[0];		// side 1
		else if (s1 & s2)
			node = node->children[1];		// side 2
		else
		{
			// both sides
			float frac = t1 / (t1 - t2);	// t1 and t2 have different signs, so - |t1-t2| > |t1|, frac in [0..1] range
			int side = t1 < t2;				// which side v1 on (child index)
			CVec3	mid;
			Lerp(v1a, v2a, frac, mid);
			// Recurse(node->children[side^1],mid,v2a)  -- later
			PUSH_NODE(node->children[side^1], mid, v2a);
			// Recurse(node->children[side],v1a,mid)
			v2a = mid;
			node = node->children[side];
		}
	}
#undef PUSH_NODE
#undef POP_NODE

	return drawLeaf;
}


/*------------------ Drawing entities --------------------*/

//?? add ent==NULL for generated sprites (set currentEntity = ?); send fxTime as param (use when ent==NULL only)
static void AddSurface(surfaceBase_t *surf, shader_t *shader, refEntity_t *ent = NULL, int numDlights = 0)
{
	int entNum = currentEntity;
	//?? !ent -- change! (should use fxTime)
	if (!ent || (ent->worldMatrix && (!shader->dependOnTime || (ent->time == vp.time)) && !shader->dependOnEntity))
		entNum = ENTITYNUM_WORLD;
	AddSurfaceToPortal(surf, shader, entNum, numDlights);
}


static void AddBspSurfaces(surfaceBase_t **psurf, int numFaces, int frustumMask, refEntity_t *e)
{
	int		j;
	refDlight_t *dl;
	int		numDlights;

	CVec3 &vieworg = e->modelvieworg;

	for (int i = 0; i < numFaces; i++)
	{
		surfaceBase_t *surf = *psurf++;
		if (surf->frame == drawFrame) continue;
		surf->frame = drawFrame;
		if (surf->dlightFrame != drawFrame) surf->dlightMask = 0;

#define CULL_SURF	\
	{				\
		STAT(gl_stats.cullSurfs++);	\
		continue;	\
	}
		//!! implement with virtual methods
		switch (surf->type)
		{
		case SURFACE_PLANAR:
			{
				surfacePlanar_t *pl = static_cast<surfacePlanar_t*>(surf);

				// backface culling
				if (gl_backfaceCull->integer)
				{
					gl_cullMode_t cull = pl->shader->cullMode;

					if (cull != CULL_NONE)
					{
						float dist = pl->plane.DistanceTo(vieworg);
						if (cull == CULL_FRONT)
						{
							if (dist < -BACKFACE_EPSILON) CULL_SURF;
						}
						else
						{
							if (dist > BACKFACE_EPSILON) CULL_SURF;
						}
					}
				}

				// frustum culling
				if (frustumMask)
				{
					if (e->worldMatrix)
					{
						if (Cull(pl->bounds, frustumMask) == FRUSTUM_OUTSIDE) CULL_SURF;
					}
					else
					{
						if (TransformedCull(pl->bounds, e) == FRUSTUM_OUTSIDE) CULL_SURF;
					}
				}

				// project dlights
				numDlights = 0;
				pl->dlights = NULL;
				if (pl->dlightMask && pl->shader->lightmapNumber != LIGHTMAP_NONE)
				{
					planeDlight_t *sdl;
					unsigned mask;

					STAT(clock(gl_stats.dlightSurf));

					sdl = pl->dlights = (planeDlight_t*)AllocDynamicMemory(sizeof(planeDlight_t) * MAX_DLIGHTS);
					if (sdl)
					{
						for (j = 0, mask = 1, dl = vp.dlights; j < vp.numDlights; j++, dl++, mask <<= 1)
							if (pl->dlightMask & mask)
							{
#define CULL_DLIGHT	\
	{				\
		surf->dlightMask &= ~mask; \
		continue;	\
	}
								CVec3 &dl_org = (e->worldMatrix) ? dl->origin : dl->modelOrg;
								float dist = pl->plane.DistanceTo(dl_org);
								if (!gl_dlightBacks->integer && dist < -8) CULL_DLIGHT;
								if (dist < 0) dist = -dist;

								if (dist >= dl->intensity) CULL_DLIGHT;
								float rad = dl->intensity * dl->intensity - dist * dist;
								rad = SQRTFAST(rad);
								float org0 = dot(dl_org, pl->axis[0]);
								if (org0 < pl->mins2[0] - rad || org0 > pl->maxs2[0] + rad) CULL_DLIGHT;
								float org1 = dot(dl_org, pl->axis[1]);
								if (org1 < pl->mins2[1] - rad || org1 > pl->maxs2[1] + rad) CULL_DLIGHT;
								// save dlight info
								sdl->pos[0] = org0;
								sdl->pos[1] = org1;
								sdl->radius = rad;
								sdl->axis   = pl->axis;
								// next dlight
								numDlights++;
								sdl++;
							}
						ResizeDynamicMemory(pl->dlights, sizeof(planeDlight_t) * numDlights);
						if (numDlights)
						{
							STAT(gl_stats.dlightSurfs++);
							STAT(gl_stats.dlightVerts += numDlights * pl->numVerts);
						}
					}
					STAT(unclock(gl_stats.dlightSurf));
				}
				else
					pl->dlightMask = 0;
			}
			break;

		case SURFACE_TRISURF:
			{
				surfaceTrisurf_t *tri = static_cast<surfaceTrisurf_t*>(surf);
				// frustum culling
				if (frustumMask)
				{
					if (e->worldMatrix)
					{
						if (Cull(tri->bounds, frustumMask) == FRUSTUM_OUTSIDE) CULL_SURF;
					}
					else
					{
						if (TransformedCull(tri->bounds, e) == FRUSTUM_OUTSIDE) CULL_SURF;
					}
				}
				// project dlights
				numDlights = 0;
				tri->dlights = NULL;
				if (tri->dlightMask && tri->shader->lightmapNumber != LIGHTMAP_NONE)
				{
					trisurfDlight_t *sdl;
					unsigned mask;

					STAT(clock(gl_stats.dlightSurf));

					sdl = tri->dlights = (trisurfDlight_t*)AllocDynamicMemory(sizeof(trisurfDlight_t) * MAX_DLIGHTS);
					if (sdl)
					{
						for (j = 0, mask = 1, dl = vp.dlights; j < vp.numDlights; j++, dl++, mask <<= 1)
							if (tri->dlightMask & mask)
							{
								CVec3 &dl_org = (e->worldMatrix) ? dl->origin : dl->modelOrg;
								// intersect surface bounds with dlight sphere
								float rad = dl->intensity;
								if (dl_org[0] - rad > tri->bounds.maxs[0] ||		//!! make separate function: sphere vs box !!
									dl_org[0] + rad < tri->bounds.mins[0] ||
									dl_org[1] - rad > tri->bounds.maxs[1] ||
									dl_org[1] + rad < tri->bounds.mins[1] ||
									dl_org[2] - rad > tri->bounds.maxs[2] ||
									dl_org[2] + rad < tri->bounds.mins[2])
								CULL_DLIGHT;
#if !TRISURF_DLIGHT_VIEWAXIS
								// compute axis for dlight projection
								CVec3	tmp, viewDir;
								CAxis	axis;										// length, width, depth
								tri->bounds.GetCenter(tmp);
								VectorSubtract(dl_org, tmp, axis[0]);				// dlight center -> surface center
								VectorSubtract(dl_org, vp.view.origin, viewDir);	//?? may be incorrect when surface belongs to model
								axis[0].NormalizeFast();
								cross(axis[0], viewDir, axis[1]);
								axis[1].NormalizeFast();
								cross(axis[0], axis[1], axis[2]);					// already normalized
								sdl->axis   = axis;
#endif // !TRISURF_DLIGHT_VIEWAXIS
								// save dlight info
								sdl->origin = dl_org;
								sdl->radius = rad;
								// next dlight
								numDlights++;
								sdl++;
							}
						if (tri->dlights)
							ResizeDynamicMemory(tri->dlights, sizeof(trisurfDlight_t) * numDlights);
						if (numDlights)
						{
							STAT(gl_stats.dlightSurfs++);
							STAT(gl_stats.dlightVerts += numDlights * tri->numVerts);
						}
					}
					STAT(unclock(gl_stats.dlightSurf));
				}
			}
			break;

		default:
			if (DEVELOPER)
				DrawTextLeft(va("bad surface: %s", *surf->shader->Name), RGB(1, 0, 0));
			continue;
#undef CULL_SURF
#undef CULL_DLIGHT
		}

		// add surface, separately process sky surfaces
		if (surf->shader->type != SHADERTYPE_SKY)
		{
			//!! apply fog
			// planar surfaces uses here vertex dlights - can remove "dlight count"
			// field to sort and draw surfaces all together, in single pass
			if (surf->shader->lightmapNumber == LIGHTMAP_VERTEX &&
				surf->type == SURFACE_PLANAR)
				numDlights = 0;
			// remember surface
			int entNum = currentEntity;
			if (entNum != ENTITYNUM_WORLD && e->worldMatrix && !surf->shader->dependOnEntity)
				entNum = ENTITYNUM_WORLD;
			AddSurfaceToPortal(surf, surf->shader, entNum, numDlights);
		}
		else
		{
			surf->AddToSky();
			if (SHOWSKY == 3)
				AddSurfaceToPortal(surf, gl_skyShader, ENTITYNUM_WORLD, 0);
		}
	}
}


bool model_t::InitEntity(entity_t *ent, refEntity_t *out)
{
	return true;
}


void model_t::AddSurfaces(refEntity_t *e)
{
	if (DEVELOPER)
		DrawText3D(e->coord.origin, va("no model: %s", *e->model->Name), RGB(1,0,0));
}


const CBspLeaf *model_t::GetLeaf(refEntity_t *e)
{
	return CM_FindLeaf(e->coord.origin);
}


void model_t::DrawLabel(refEntity_t *e)
{
//	DrawText3D(e->center, va("origin: %g %g %g\nmodel: %s",
//		VECTOR_ARG(e->origin), name), RGB(0.4,0.1,0.2));
}


bool inlineModel_t::InitEntity(entity_t *ent, refEntity_t *out)
{
	if (flags & CMODEL_NODRAW) return false;
	CVec3 v;
	bounds.GetCenter(v);
	VectorSubtract(bounds.maxs, v, out->size2);		// half-size
	out->coord.UnTransformPoint(v, out->center);
	out->radius = radius;
	if (color.c[3] != 255)
	{
		if (color.c[3] == 0) return false;				// alpha==0, do not draw model
		out->shaderColor.c[3] = color.c[3];
	}
	return true;
}


void inlineModel_t::AddSurfaces(refEntity_t *e)
{
	int		i;
	refDlight_t *dl;
	unsigned mask;

	// check dlights
	unsigned dlightMask = 0;
	for (i = 0, dl = vp.dlights, mask = 1; i < vp.numDlights; i++, dl++, mask <<= 1)
	{
		CVec3	tmp;
		VectorSubtract(e->center, dl->origin, tmp);
		float dist2 = dot(tmp, tmp);
		float dist2min = radius + dl->intensity;
		dist2min = dist2min * dist2min;
		if (dist2 >= dist2min) continue;	// too far

		if (!e->worldMatrix) e->coord.TransformPoint(dl->origin, dl->modelOrg);
		dlightMask |= mask;
	}
	// mark surfaces
	surfaceBase_t	*surf, **psurf;
	if (dlightMask)
		for (i = 0, psurf = faces; i < numFaces; i++, psurf++)
		{
			surf = *psurf;
			surf->dlightFrame = drawFrame;
			surf->dlightMask  = dlightMask;
		}
	AddBspSurfaces(faces, numFaces, e->frustumMask, e);
}


const CBspLeaf *inlineModel_t::GetLeaf(refEntity_t *e)
{
	e->frustumMask = 0xFF;		// for updating
	int ret = SphereCull(e->center, e->radius, &e->frustumMask);
	// try to cull bounding sphere (faster than box cull)
	if (ret == FRUSTUM_OUTSIDE)
		return NULL;
	// frustum culling (entire model)
	if (ret & FRUSTUM_CENTER_OUTSIDE)
		if (TransformedCull(bounds, e) == FRUSTUM_OUTSIDE)
			return NULL;

	return SphereLeaf(e->center, e->radius);
}


void inlineModel_t::DrawLabel(refEntity_t *e)
{
	DrawText3D(e->center, va("origin: %g %g %g\nbmodel: %s\nflags: $%X",
		VECTOR_ARG(e->coord.origin), *Name, e->flags), RGB(0.1,0.4,0.2));
}


bool md3Model_t::InitEntity(entity_t *ent, refEntity_t *out)
{
	if (!numSurfaces) return false;

	// sanity check
	if (out->frame >= numFrames || out->frame < 0)
	{
		if (DEVELOPER)
			DrawTextLeft(va("md3Model_t::InitEntity: no frame %d in %s\n", out->frame, *Name), RGB(1,0,0));
		out->frame = out->oldFrame = 0;
	}
	if (out->oldFrame >= numFrames || out->oldFrame < 0)
	{
		if (DEVELOPER)
			DrawTextLeft(va("md3Model_t::InitEntity: no frame %d in %s\n", out->oldFrame, *Name), RGB(1,0,0));
		out->frame = out->oldFrame = 0;
	}
	md3Frame_t *frame1 = frames + out->frame;
	md3Frame_t *frame2 = frames + out->oldFrame;
	// lerp center
	CVec3 center1, center2;
	if (out->drawScale != 1)
	{
		VectorScale(frame1->localOrigin, out->drawScale, center1);
		VectorScale(frame2->localOrigin, out->drawScale, center2);
	}
	else
	{
		center1 = frame1->localOrigin;
		center2 = frame2->localOrigin;
	}
	out->coord.UnTransformPoint(center1, center1);
	out->coord.UnTransformPoint(center2, center2);
	Lerp(center1, center2, out->backLerp, out->center);
	// lerp radius
	out->radius = Lerp(frame1->radius, frame2->radius, out->backLerp) * out->drawScale;
	// compute mins/maxs (lerp ??)
#if 0
	//!!!! HERE: use frame bounding sphere for gl_showbboxes visualization
	out->size2[0] = out->size2[1] = out->size2[2] = frame1->radius * out->drawScale;
#else
	VectorSubtract(frame1->bounds.maxs, frame1->localOrigin, out->size2);
	if (out->drawScale != 1)
		out->size2.Scale(out->drawScale);
#endif
	if (ent->skin && ent->skin->numSurfs == 1 && ent->skin->surf[0].Name[0] == 0)
		out->customShader = static_cast<shader_t*>(ent->skin->surf[0].shader);
	return true;
}


//#define SHOW_MD3_SURFS	1

void md3Model_t::AddSurfaces(refEntity_t *e)
{
#if SHOW_MD3_SURFS
	DrawTextLeft(va("%s: custSh=%s skin=%X skinNum=%d", *Name, e->customShader ? *e->customShader->Name : "NULL", e->skin, e->skinNum));
	if (e->skin)
		for (int k = 0; k < e->skin->numSurfs; k++)
			DrawTextLeft(va("  %d: %s = %s", k, *e->skin->surf[k].Name, *e->skin->surf[k].shader->Name),RGB(0.1,0.1,1));
#endif
	surfaceMd3_t *s = surf;
	for (int i = 0; i < numSurfaces; i++, s++)
	{
		shader_t *shader = NULL;
		//!! fog
		// choose skin to draw
		if (e->customShader)			// used for fx (overrided skin)
			shader = e->customShader;
		else if (e->skin)
		{
			for (int j = 0; j < e->skin->numSurfs; j++)
				if (e->skin->surf[j].Name == s->Name)
				{
					shader = static_cast<shader_t*>(e->skin->surf[j].shader);
#if SHOW_MD3_SURFS
					DrawTextLeft(va("  %s for %s", *shader->Name, *s->Name));
#endif
					break;
				}
			if (!shader)
			{
				// it seems, this surface is not listed (or "nodraw" shader used) -- skip it
#if SHOW_MD3_SURFS
				DrawTextLeft(va("  skip: %s", *s->Name));
#endif
				continue;
			}
		}
		else if (e->skinNum >= 0 && e->skinNum < s->numShaders)
			shader = s->shaders[e->skinNum];

		if (!shader && s->numShaders) shader = s->shaders[0];
		if (!shader) shader = FindShader("*identityLight", SHADER_SKIN); // white + diffuse lighting

		if (e->flags & RF_TRANSLUCENT)
			shader = GetAlphaShader(shader);
		// draw surface
		AddSurface(s, shader, e);
	}
}


const CBspLeaf *md3Model_t::GetLeaf(refEntity_t *e)
{
	if (SphereCull(e->center, e->radius, NULL) == FRUSTUM_OUTSIDE)
		return NULL;
	return SphereLeaf(e->center, e->radius);
}


void md3Model_t::DrawLabel(refEntity_t *e)
{
	DrawText3D(e->center, va("origin: %g %g %g\nmd3: %s\nskin: %s\nflags: $%X  scale: %g",
		VECTOR_ARG(e->coord.origin), *Name, e->customShader ? *e->customShader->Name : "(default)", e->flags, e->drawScale),
		RGB(0.1,0.4,0.2));
		//?? incorrect skin info for md3 multi-surface models
}


bool sprModel_t::InitEntity(entity_t *ent, refEntity_t *out)
{
	out->center      = out->coord.origin;
	out->radius      = radius;
	out->worldMatrix = true;
	return true;
}


void sprModel_t::AddSurfaces(refEntity_t *e)
{
	surfacePoly_t *p = (surfacePoly_t*)AllocDynamicMemory(sizeof(surfacePoly_t) + (4-1) * sizeof(vertexPoly_t));
	if (!p)		// out of dynamic memory
		return;
	CALL_CONSTRUCTOR(p);

	p->numVerts = 4;

	sprFrame_t *frame = &frames[e->frame % numFrames];
	// setup xyz
	CVec3	down, up2;
	VectorMA(e->coord.origin, -frame->localOrigin[1], vp.view.axis[2], down);
	VectorMA(down, -frame->localOrigin[0], vp.view.axis[1], p->verts[0].xyz);	// 0
	VectorMA(down, frame->width - frame->localOrigin[0], vp.view.axis[1], p->verts[1].xyz);	// 1
	VectorScale(vp.view.axis[2], frame->height, up2);
	VectorAdd(p->verts[0].xyz, up2, p->verts[3].xyz);	// 3
	VectorAdd(p->verts[1].xyz, up2, p->verts[2].xyz);	// 2

	// setup st
	p->verts[0].st[0] = p->verts[3].st[0] = 1;
	p->verts[1].st[0] = p->verts[2].st[0] = 0;
	p->verts[0].st[1] = p->verts[1].st[1] = 1;
	p->verts[2].st[1] = p->verts[3].st[1] = 0;
	// setup color
	unsigned color = e->shaderColor.rgba;
	if (!(e->flags & RF_TRANSLUCENT))
		color |= RGBA(0,0,0,1);						// make it non-transparent
	p->verts[0].c.rgba = p->verts[1].c.rgba = p->verts[2].c.rgba = p->verts[3].c.rgba = color;

	AddSurface(p, frame->shader, e);
}


const CBspLeaf *sprModel_t::GetLeaf(refEntity_t *e)
{
	return AlphaSphereLeaf(e->coord.origin, e->radius);
}


static void AddBeamSurfaces(const beam_t *b)
{
	CVec3	viewDir, tmp;

	// compute level of detail
	VectorSubtract(b->drawStart, vp.view.origin, viewDir);
	float z1 = dot(viewDir, vp.view.axis[0]);	// beamStart.Z
	VectorSubtract(b->drawEnd, vp.view.origin, tmp);
	float z2 = dot(tmp, vp.view.axis[0]);		// beamEnd.Z
	float size = min(z1, z2);

	size = b->radius * 200 / (size * vp.fov_scale);
	int numParts = appRound(size);
	numParts = bound(numParts, 1, 6);

	// compute beam axis
	CVec3	axis[3];		// length, width, depth
	VectorSubtract(b->drawEnd, b->drawStart, axis[0]);
	axis[0].NormalizeFast();
	cross(axis[0], viewDir, axis[1]);
	axis[1].NormalizeFast();
	cross(axis[0], axis[1], axis[2]);			// already normalized

	CVec3	dir1, dir2;
	VectorScale(axis[1], b->radius, dir2);
	float angle = 0;
	float angleStep = 0.5f / numParts;			// 0.5 -- PI/2
	color_t	color;
	color.rgba = gl_config.tbl_8to32[b->color.c[0]];
	color.c[3] = b->color.c[3];
	for (int i = 0; i < numParts; i++)
	{
		//!! use SURF_TRISURF to allocate all parts in one surface with shared verts
		surfacePoly_t *p = (surfacePoly_t*)AllocDynamicMemory(sizeof(surfacePoly_t) + (4-1) * sizeof(vertexPoly_t));
		if (!p)		// out of dynamic memory
			return;
		CALL_CONSTRUCTOR(p);

		p->numVerts = 4;

		// rotate dir1 & dir2 vectors
		dir1 = dir2;
		angle += angleStep;
		float sx = SIN_FUNC(angle) * b->radius;
		float cx = COS_FUNC(angle) * b->radius;
		VectorScale(axis[1], cx, dir2);
		VectorMA(dir2, sx, axis[2]);

		// setup xyz
		VectorAdd(b->drawStart, dir1, p->verts[0].xyz);
		VectorAdd(b->drawEnd,   dir1, p->verts[1].xyz);
		VectorAdd(b->drawStart, dir2, p->verts[3].xyz);
		VectorAdd(b->drawEnd,   dir2, p->verts[2].xyz);

		// setup st
		p->verts[0].st[0] = p->verts[3].st[0] = 1;
		p->verts[1].st[0] = p->verts[2].st[0] = 0;
		p->verts[0].st[1] = p->verts[1].st[1] = 1;
		p->verts[2].st[1] = p->verts[3].st[1] = 0;

		p->verts[0].c.rgba = p->verts[1].c.rgba = p->verts[2].c.rgba = p->verts[3].c.rgba = color.rgba;

		//!! can be different shader to provide any types of lined particles
		AddSurface(p, gl_identityLightShader2);
	}
}


#define CYLINDER_PARTS	16		// should be even number
#define CYLINDER_FIX_ALPHA		1
#define MIN_FIXED_ALPHA			0.2f

static void AddCylinderSurfaces(const beam_t *b)
{
	CVec3	viewDir;
	CVec3	axis[3];							// length, width, depth

	//?? cumpute LOD
	VectorSubtract(b->drawStart, vp.view.origin, viewDir);
	// compute beam axis
	VectorSubtract(b->drawEnd, b->drawStart, axis[0]);
	float len = axis[0].NormalizeFast();
	cross(axis[0], viewDir, axis[1]);
	axis[1].NormalizeFast();
	cross(axis[0], axis[1], axis[2]);			// already normalized

	float st0 = VectorDistance(b->drawEnd, b->end);

#if CYLINDER_FIX_ALPHA
	// compute minimal distance to beam
	float f = dot(viewDir, axis[0]);
	CVec3 v;
	VectorMA(b->drawStart, -f, axis[0], v);		// v is a nearest point on beam line
	VectorSubtract(v, vp.view.origin, v);
	f = dot(v, v);
	f = SQRTFAST(f);							// distance to line
	float fixAngle;
	if (f <= b->radius)
		fixAngle = -1;
	else
	{
		// here: f > beamRadius (should be > 0)
		f = b->radius / f;
		fixAngle = ASIN_FUNC(f) / (2 * M_PI);
	}

#endif

	float angle, anglePrev, angleStep;
	for (int i = 0; i < CYLINDER_PARTS; i++)
	{
		// cylinder will be drawn with 2 passes, from far to near
		// (for correct image when used shader with depthwrites)
		switch (i)
		{
		case 0:										// starting
			angle = -0.25f;
			angleStep = 1.0f / CYLINDER_PARTS;
			break;
		case CYLINDER_PARTS/2:
			angle = 0.75f;
			angleStep = -1.0f / CYLINDER_PARTS;
			break;
		}

		surfacePoly_t *p = (surfacePoly_t*)AllocDynamicMemory(sizeof(surfacePoly_t) + (4-1) * sizeof(vertexPoly_t));
		if (!p)		// out of dynamic memory
			return;
		CALL_CONSTRUCTOR(p);

		p->numVerts = 4;

		// rotate dir1 & dir2 vectors
		anglePrev = angle;
		if (i == CYLINDER_PARTS/2-1 || i == CYLINDER_PARTS-1)
			angle = 0.25f;
		else
			angle += angleStep;

		float	sx, cx;
		CVec3	dir1, dir2;

		sx = SIN_FUNC(anglePrev) * b->radius;
		cx = COS_FUNC(anglePrev) * b->radius;
		VectorScale(axis[1], cx, dir1);
		VectorMA(dir1, sx, axis[2]);

		sx = SIN_FUNC(angle) * b->radius;
		cx = COS_FUNC(angle) * b->radius;
		VectorScale(axis[1], cx, dir2);
		VectorMA(dir2, sx, axis[2]);

		// setup xyz
		VectorAdd(b->drawStart, dir1, p->verts[0].xyz);
		VectorAdd(b->drawEnd,   dir1, p->verts[1].xyz);
		VectorAdd(b->drawStart, dir2, p->verts[3].xyz);
		VectorAdd(b->drawEnd,   dir2, p->verts[2].xyz);

		p->verts[0].st[1] = p->verts[3].st[1] = len + st0;
		p->verts[1].st[1] = p->verts[2].st[1] = st0;
		p->verts[0].st[0] = p->verts[1].st[0] = anglePrev;
		p->verts[2].st[0] = p->verts[3].st[0] = angle;

		p->verts[0].c.rgba = p->verts[1].c.rgba = p->verts[2].c.rgba = p->verts[3].c.rgba = b->color.rgba;
#if CYLINDER_FIX_ALPHA
		// fix alpha: make it depends on angle (better image quality)
		if (fixAngle >= 0)
		{
			float	a1, a2;

			if (i < CYLINDER_PARTS / 2)
			{	// angles in -0.25..0.25 range
				a1 = fabs(anglePrev - fixAngle);
				a2 = fabs(angle - fixAngle);
			}
			else
			{	// angles in 0.25..0.72 range
				a1 = fabs(anglePrev - 0.5f + fixAngle);
				a2 = fabs(angle - 0.5f + fixAngle);
			}
			if (a1 > 0.25f) a1 = 0.25f;
			if (a2 > 0.25f) a2 = 0.25f;
			a1 = Lerp(MIN_FIXED_ALPHA, 1.0f, a1 * 4);
			a2 = Lerp(MIN_FIXED_ALPHA, 1.0f, a2 * 4);

			p->verts[0].c.c[3] = p->verts[1].c.c[3] = appRound(b->color.c[3] * a1);
			p->verts[2].c.c[3] = p->verts[3].c.c[3] = appRound(b->color.c[3] * a2);
		}
#endif

		AddSurface(p, static_cast<shader_t*>(b->shader));
	}
}


#define BEAM_PARTS	3		// 1 - flat

static void AddStarBeam(const beam_t *b)
{
	CVec3	viewDir;
	CVec3	axis[3];		// length, width, depth

	VectorSubtract(b->drawStart, vp.view.origin, viewDir);
	// compute beam axis
	VectorSubtract(b->drawEnd, b->drawStart, axis[0]);
	float len = axis[0].NormalizeFast();
	cross(axis[0], viewDir, axis[1]);
	axis[1].NormalizeFast();
	cross(axis[0], axis[1], axis[2]);			// already normalized

	float st0 = VectorDistance(b->drawEnd, b->end);

	float angle = 0;
	for (int i = 0; i < BEAM_PARTS; i++)
	{
		surfacePoly_t *p = (surfacePoly_t*)AllocDynamicMemory(sizeof(surfacePoly_t) + (4-1) * sizeof(vertexPoly_t));
		if (!p)		// out of dynamic memory
			return;
		CALL_CONSTRUCTOR(p);

		p->numVerts = 4;

		float sx = SIN_FUNC(angle) * b->radius;
		float cx = COS_FUNC(angle) * b->radius;
		CVec3	dir1, dir2;
		VectorScale(axis[1], cx, dir1);
		VectorMA(dir1, sx, axis[2]);
		VectorNegate(dir1, dir2);

		angle += 0.5f / BEAM_PARTS;

		// setup xyz
		VectorAdd(b->drawStart, dir1, p->verts[0].xyz);
		VectorAdd(b->drawEnd,   dir1, p->verts[1].xyz);
		VectorAdd(b->drawStart, dir2, p->verts[3].xyz);
		VectorAdd(b->drawEnd,   dir2, p->verts[2].xyz);

		p->verts[0].st[1] = p->verts[3].st[1] = len + st0;
		p->verts[1].st[1] = p->verts[2].st[1] = st0;
		p->verts[0].st[0] = p->verts[1].st[0] = 0;
		p->verts[2].st[0] = p->verts[3].st[0] = 1;

		p->verts[0].c.rgba = p->verts[1].c.rgba = p->verts[2].c.rgba = p->verts[3].c.rgba = b->color.rgba;

		AddSurface(p, static_cast<shader_t*>(b->shader));
	}
}


/*------------------ Drawing world -------------------*/


static const CBspLeaf *WalkBspTree()
{
	guard(WalkBspTree);

	int sptr = 0;
	struct {
		CBspNode *node;
		int		frustumMask;
		unsigned dlightMask;
	} stack[MAX_TREE_DEPTH], *st;

	CBspNode *node = bspfile.nodes;
	int frustumMask = MAX_FRUSTUM_MASK;	// check all frustum planes
	unsigned dlightMask = vp.numDlights >= 32 ? 0xFFFFFFFF : (1 << vp.numDlights) - 1;
	const CBspLeaf *firstLeaf = NULL, *lastLeaf = NULL;
	int drawOrder = 0;

#define PUSH_NODE(n,dl)					\
	st = &stack[sptr++];				\
	st->node        = n;				\
	st->frustumMask = frustumMask;		\
	st->dlightMask  = dl;

	// if sptr == 0 -- whole tree visited
#define POP_NODE()						\
	if (!sptr)							\
		node = NULL;					\
	else								\
	{									\
		st = &stack[--sptr];			\
		node        = st->node;			\
		frustumMask = st->frustumMask;	\
		dlightMask  = st->dlightMask;	\
	}

	while (node)			// when whole tree visited - node = NULL
	{
		if (node->ex->visFrame != visFrame)
		{	// discard node/leaf using visinfo
			POP_NODE();
			continue;
		}

		if (gl_frustumCull->integer)
		{
			/*-------- frustum culling ----------*/
#define CULL_NODE(bit)	\
			if (frustumMask & (1<<bit))	\
			{							\
				switch (node->bounds.OnPlaneSide(vp.frustum[bit])) {	\
				case 1:					\
					frustumMask &= ~(1<<bit);	\
					break;				\
				case 2:					\
					POP_NODE();			\
					continue;			\
				}						\
			}

			CULL_NODE(0);
			CULL_NODE(1);
			CULL_NODE(2);
			CULL_NODE(3);
			CULL_NODE(4);
#undef CULL_NODE
		}

#if 0
		if (gl_oCull->integer >= 2 && node->isNode)	// && sptr >= 2 && sptr <= 5)
		{
			static int tests;
			static int frm;
			bool	occl;

			/* NOTES:
			 *   1. 1st version mostly faster (sometimes reverse !)
			 *   2. its faster to cull every node, than cull every leaf
			 */
			if (gl_oCull->integer == 2)
				occl = WorldBoxOccluded(node->bounds);
			else
			{	// >= 3
				static refEntity_t ent;		// just zeroed entity
				CVec3	v, h;

				node->bounds.GetCenter(ent.center);		//?? can pre-compute on map loading
				VectorSubtract(node->bounds.maxs, ent.center, ent.size2);
				ent.worldMatrix = true;
				occl = BoxOccluded(&ent, ent.size2);
				if (occl && gl_oCull->integer == 4)
				{
					float	mins2[2], maxs2[2];

					GetBoxRect(&ent, ent.size2, mins2, maxs2);
					VectorMA(ent.center, mins2[0], vp.view.axis[1], h);
					VectorMA(h, mins2[1], vp.view.axis[2], v);
#if 1
					DrawText3D(v, "*", RGB(0.2,0.6,0.1));
					VectorMA(h, maxs2[1], vp.view.axis[2], v);
					DrawText3D(v, "*", RGB(0.2,0.6,0.1));
					VectorMA(ent.center, maxs2[0], vp.view.axis[1], h);
					VectorMA(h, mins2[1], vp.view.axis[2], v);
					DrawText3D(v, "*", RGB(0.2,0.6,0.1));
					VectorMA(h, maxs2[1], vp.view.axis[2], v);
					DrawText3D(v, "*", RGB(0.2,0.6,0.1));
#else
					DrawText3D(v, va("(%d: %gx%gx%g)", sptr, ent.maxs[0]*2,ent.maxs[1]*2,ent.maxs[2]*2), RGB(0.3,0.9,0.2));
#endif
				}
			}
			if (occl)
			{
				tests++;
				POP_NODE();
				continue;
			}
			// stats
			if (frm != drawFrame)
			{
				DrawTextLeft(va("occl: %d", tests));
				frm = drawFrame;
				tests = 0;
			}
		}
#endif

		node->ex->drawOrder = drawOrder++;
		if (!node->isLeaf)
		{
			unsigned dlight0, dlight1;

			// check dlights
			dlight0 = dlight1 = 0;
			if (dlightMask)
			{
				unsigned i, mask;
				refDlight_t *dl;
				float	d;

				for (i = 0, dl = vp.dlights, mask = 1; i < MAX_DLIGHTS; i++, dl++, mask <<= 1)
				{
					if (!(dlightMask & mask)) continue;
					d = node->plane->DistanceTo(dl->origin);
					if (d > -dl->intensity) dlight0 |= mask;
					if (d <  dl->intensity) dlight1 |= mask;
				}
			}

			if (node->plane->DistanceTo(vp.view.origin) < 0)
			{
				// ch[0], then ch[1]
				PUSH_NODE(node->children[1], dlight1);
				node = node->children[0];
				dlightMask = dlight0;
			}
			else
			{
				// ch[1], then ch[0]
				PUSH_NODE(node->children[0], dlight0);
				node = node->children[1];
				dlightMask = dlight1;
			}
			continue;
		}

		// HERE: node is leaf
		// NOTE: leaf may be without faces, but we can insert into this leaf some model, which
		//  should be painted in BSP order; so - we should add leaf to draw order in any case
		// Following condition is for Q1/HL bsp only: these maps have common leaf with
		// CONTENTS_SOLID (leaf #0) - it have no surfaces, but we should draw non-surface leafs ...
		// What we can do:
		// a) split all leafs #0 -> new leafs (as cmodel.cpp do)
		// b) remove links leaf #0 from BSP (require tree modification)
		const CBspLeaf *leaf = (CBspLeaf*)node;
		if (leaf->ex->frame != drawFrame)
		{
			if (!firstLeaf)
				firstLeaf = leaf;
			else
				lastLeaf->ex->drawNext = leaf;
			lastLeaf = leaf;
			STAT(gl_stats.frustLeafs++);

			leaf->ex->frame        = drawFrame;
			leaf->ex->frustumMask  = frustumMask;
			leaf->ex->drawEntity   = NULL;
			leaf->ex->drawParticle = NULL;
			leaf->ex->drawBeam     = NULL;
			// mark dlights on leaf surfaces
			if (dlightMask)
			{
				int		i;
				surfaceBase_t **psurf, *surf;

				for (i = 0, psurf = leaf->ex->faces; i < leaf->numFaces; i++, psurf++)
				{
					surf = *psurf;
					if (surf->dlightFrame == drawFrame)
						surf->dlightMask  |= dlightMask;
					else
					{
						surf->dlightMask  = dlightMask;
						surf->dlightFrame = drawFrame;
					}
				}
			}
		}

		POP_NODE();
	}

	// mark end of draw sequence
	if (lastLeaf)
		lastLeaf->ex->drawNext = NULL;

#undef PUSH_NODE
#undef POP_NODE
	return firstLeaf;

	unguard;
}


static void DrawEntities(int firstEntity, int numEntities)
{
	guard(DrawEntities);

	int		i;
	refEntity_t	*e;
	for (i = 0, e = gl_entities + firstEntity; i < numEntities; i++, e++)
	{
		e->visible = false;
		if (!e->model)
		{
			if (e->flags & RF_BBOX)
			{
				// just add surface and continue
				surfaceEntity_t *surf = (surfaceEntity_t*)AllocDynamicMemory(sizeof(surfaceEntity_t));
				if (surf)
				{
					CALL_CONSTRUCTOR(surf);
					surf->entity = e;
					AddSurfaceToPortal(surf, gl_entityShader, ENTITYNUM_WORLD);
				}
			}
			else
				DrawText3D(e->coord.origin, va("* bad ent %d: f=%X", i, e->flags), RGB(1,0,0));
			continue;
		}

#if !NO_DEBUG
		if (!r_drawentities->integer) continue;		// do not draw entities with model in this mode
#endif

		const CBspLeaf *leaf = e->model->GetLeaf(e);
		if (!leaf)
		{
			// entity do not occupy any visible leafs
			STAT(gl_stats.cullEnts++);
			continue;
		}

		// calc model distance
		CVec3 delta;
		VectorSubtract(e->center, vp.view.origin, delta);
		float dist2 = e->dist2 = dot(delta, vp.view.axis[0]);	// get Z-coordinate

		// occlusion culling
		if (e->model && gl_oCull->integer && !(e->flags & RF_DEPTHHACK) &&
			(e->model->type == MODEL_INLINE || e->model->type == MODEL_MD3))
			//?? sprites too (depend on size/dist; use 4 its points to cull)
		{
			if (BoxOccluded(e, e->size2))
			{
#if !NO_DEBUG
				if (gl_labels->integer == 2)
					DrawText3D(e->center, va("occluded\n%s", *e->model->Name), RGB(0.1,0.2,0.4));
#endif
				STAT(gl_stats.ocullEnts++);
				continue;
			}
		}

#if !NO_DEBUG
		if (e->model && gl_labels->integer)
			e->model->DrawLabel(e);
#endif

		e->visible = true;

		// add entity to leaf's entity list
		if (leaf->ex->drawEntity)
		{
			refEntity_t *prev, *e1;

			for (e1 = leaf->ex->drawEntity, prev = NULL; e1; prev = e1, e1 = e1->drawNext)
				if (dist2 > e1->dist2)
				{
					e->drawNext = e1;
					if (prev)	prev->drawNext = e;
					else     	leaf->ex->drawEntity = e;	// insert first
					break;
				}
			if (!e1)	// insert last
			{
				prev->drawNext = e;
				e->drawNext = NULL;
			}
		}
		else
		{	// first entity for this leaf
			leaf->ex->drawEntity = e;
			e->drawNext = NULL;
		}

		if (e->model) SetupModelMatrix(e);
	}

	unguard;
}


static void DrawParticles()
{
	const CBspLeaf *leaf;

	for (particle_t *p = vp.particles; p; p = p->next)
	{
		leaf = p->leaf;
		if (leaf->ex->frame == drawFrame)
		{
			p->drawNext = leaf->ex->drawParticle;
			leaf->ex->drawParticle = p;
		}
		else
			STAT(gl_stats.cullParts++);

		STAT(gl_stats.parts++);
	}

	for (beam_t *b = vp.beams; b; b = b->next)
	{
		b->drawStart = b->start;
		b->drawEnd = b->end;
		if (!CutCylinder(b->drawStart, b->drawEnd, b->radius))
		{
			STAT(gl_stats.cullParts++);
			continue;
		}
		else
		{
			CVec3	center;
			VectorAdd(b->drawStart, b->drawEnd, center);
			center.Scale(0.5f);
			leaf = BeamLeaf(b->drawStart, b->drawEnd);
		}

		if (leaf)
		{
			b->drawNext = leaf->ex->drawBeam;
			leaf->ex->drawBeam = b;
		}
		else
			STAT(gl_stats.cullParts++);
		STAT(gl_stats.parts++);
	}
}


#define FLARE_DIST0		40
#define FLARE_DIST1		256
#define FLARE_DIST2		768		// Cvar_VariableValue("flare")
#define FLARE_FADE		0.2		// Cvar_VariableValue("flare")
#define SUN_DRAWDIST	32

static void DrawFlares()
{
	for (gl_flare_t *f = map.flares; f ; f = f->next)
	{
		float	scale;
		CVec3	tmp, flarePos;
		color_t	color;

		bool cull = false;

		if (f->radius >= 0)
		{
			inlineModel_t *im;

			if (f->surf && f->surf->frame != drawFrame) cull = true;
			flarePos = f->origin;
			if (im = f->owner)
			{	// flare linked to entity - shift it with entity origin
				refEntity_t *e;
				int		i;

				// check entity visibility (can add entity origin only when it is valid for current frame)
				if (!f->surf || cull)					// culled/fading flare
				{
					surfaceBase_t **s;

					for (i = 0, s = im->faces; i < im->numFaces; i++, s++)
						if ((*s)->frame == drawFrame) break;
					if (i == im->numFaces) continue;	// no visible faces - skip flare
				}
				// find entity
				for (i = 0, e = gl_entities + vp.firstEntity; i < vp.numEntities; i++, e++)
					if (e->model == im)
					{
						i = -1;
						break;
					}
				if (i > 0) continue;					// should not happens ...
				// compute flare origin
				CVec3 tmp;
				im->bounds.GetCenter(tmp);
				// flarePos = e->center - im->center + flarePos
				VectorSubtract(e->center, tmp, tmp);
				flarePos.Add(tmp);
//				DrawTextLeft(va("flare shift: %g %g %g -> flarePos: %g %g %g", VECTOR_ARG(tmp), VECTOR_ARG(flarePos)));
			}
			// perform PVS cull for flares with radius 0 (if flare have radius > 0
			// it (mostly) will be placed inside invisible (solid) leaf)
			if (f->radius == 0 && !im && f->leaf->ex->frame != drawFrame) cull = true;
		}
		else
		{
			// sun flare
			if (gl_state.useFastSky || gl_skyShader == gl_defaultSkyShader)
				continue;	// no flare with this sky
			VectorMA(vp.view.origin, SUN_DRAWDIST, f->origin, flarePos);
			scale = f->size * SUN_DRAWDIST / (2 * FLARE_DIST1);
		}

		// should perform frustum culling even if flare not in PVS:
		// can be occluded / outside frustum -- fade / hide
		if (PointCull(flarePos, (f->owner || cull || f->radius < 0) ? MAX_FRUSTUM_MASK : f->leaf->ex->frustumMask) == FRUSTUM_OUTSIDE)
		{
			STAT(gl_stats.cullFlares++);				// outside frustum - do not fade
			continue;
		}
		if (f->radius > 0)
		{
			if (!SphereLeaf(flarePos, f->radius)) cull = true;
		}

		color.rgba = f->color.rgba;
		int style = vp.lightStyles[f->style].value;

		// get viewsize
		if (f->radius >= 0)
		{
			scale = f->size / 2;

			// get Z-coordinate
			CVec3 tmp;
			VectorSubtract(flarePos, vp.view.origin, tmp);
			float dist = dot(tmp, vp.view.axis[0]);

			if (dist < FLARE_DIST0)			// too near - do not fade
			{
				STAT(gl_stats.cullFlares++);
				continue;
			}
			if (dist < FLARE_DIST1)
				style = appRound(style * (dist - FLARE_DIST0) / (FLARE_DIST1 - FLARE_DIST0));
//				scale = scale * (dist - FLARE_DIST0) / (FLARE_DIST1 - FLARE_DIST0);
			else if (dist > FLARE_DIST2)
				scale = scale * dist / FLARE_DIST2;
		}

		if (!cull)
		{
			trace_t	trace;

			STAT(clock(gl_stats.flareTrace));
			STAT(gl_stats.testFlares++);
			// check visibility with trace
			if (f->radius >= 0)
			{
				CM_BoxTrace(trace, vp.view.origin, flarePos, nullBox, 0, CONTENTS_SOLID);
				ClipTraceToEntities(trace, vp.view.origin, flarePos, CONTENTS_SOLID);
				if (trace.fraction < 1 && (f->radius <= 0 || (VectorDistance(trace.endpos, flarePos) > f->radius)))
					cull = true;
			}
			else
			{	// sun flare
				CVec3	tracePos;

				VectorMA(vp.view.origin, BIG_NUMBER, f->origin, tracePos);
				CM_BoxTrace(trace, vp.view.origin, tracePos, nullBox, 0, CONTENTS_SOLID);
				ClipTraceToEntities(trace, vp.view.origin, tracePos, CONTENTS_SOLID);
				if (!(trace.fraction < 1 && trace.surface->flags & SURF_SKY))
					cull = true;
			}
			STAT(unclock(gl_stats.flareTrace));

			if (!cull)
				f->lastTime = vp.time;
		}

		if (cull)
		{
			// fade flare
			float timeDelta = (vp.time - f->lastTime) / FLARE_FADE;
			if (timeDelta >= 1)
			{
				STAT(gl_stats.cullFlares++);
				continue;
			}
			style = appRound((1 - timeDelta) * style);
		}

		// alloc surface
		surfacePoly_t *p = (surfacePoly_t*)AllocDynamicMemory(sizeof(surfacePoly_t) + (4-1) * sizeof(vertexPoly_t));
		if (!p) return;
		CALL_CONSTRUCTOR(p);

		p->numVerts = 4;

		// setup xyz
		VectorMA(flarePos, -scale, vp.view.axis[2], tmp);			// down
		VectorMA(tmp, -scale, vp.view.axis[1], p->verts[0].xyz);	// 0
		VectorMA(tmp, scale, vp.view.axis[1], p->verts[1].xyz);	// 1
		VectorScale(vp.view.axis[2], scale * 2, tmp);				// up-down
		VectorAdd(p->verts[0].xyz, tmp, p->verts[3].xyz);			// 3
		VectorAdd(p->verts[1].xyz, tmp, p->verts[2].xyz);			// 2

		// setup st
		p->verts[0].st[0] = p->verts[3].st[0] = 1;
		p->verts[1].st[0] = p->verts[2].st[0] = 0;
		p->verts[0].st[1] = p->verts[1].st[1] = 1;
		p->verts[2].st[1] = p->verts[3].st[1] = 0;

		// setup color
		if (style != 128)
		{
			int r = color.c[0] * style >> 7;
			int g = color.c[1] * style >> 7;
			int b = color.c[2] * style >> 7;
			if (style > 128)
				NORMALIZE_COLOR255(r, g, b);
			color.c[0] = r;
			color.c[1] = g;
			color.c[2] = b;
		}
		p->verts[0].c.rgba = p->verts[1].c.rgba = p->verts[2].c.rgba = p->verts[3].c.rgba = color.rgba;

		AddSurfaceToPortal(p, gl_flareShader, ENTITYNUM_WORLD);
	}

	STAT(gl_stats.flares = map.numFlares);
}


static void DrawBspSequence(const CBspLeaf *leaf)
{
	guard(DrawBspSequence);

	for ( ; leaf; leaf = leaf->ex->drawNext)
	{
		// update world bounding box
		vp.bounds.Expand(leaf->bounds);
		// add leafFaces to draw list
		currentEntity = ENTITYNUM_WORLD;
		AddBspSurfaces(leaf->ex->faces, leaf->numFaces, leaf->ex->frustumMask, &gl_entities[ENTITYNUM_WORLD]);

		/*---------- draw leaf entities -----------*/

		for (refEntity_t *e = leaf->ex->drawEntity; e; e = e->drawNext)
		{
			currentEntity = e - gl_entities;
			if (e->model) e->model->AddSurfaces(e);
		}

		/*------------ draw particles -------------*/

		if (leaf->ex->drawParticle)
		{
			surfaceParticle_t *surf = (surfaceParticle_t*) AllocDynamicMemory(sizeof(surfaceParticle_t));
			if (surf)
			{
				CALL_CONSTRUCTOR(surf);
				surf->part = leaf->ex->drawParticle;
				AddSurfaceToPortal(surf, gl_particleShader, ENTITYNUM_WORLD);
			}
		}

		for (beam_t *b = leaf->ex->drawBeam; b; b = b->drawNext)
		{
			switch (b->type)
			{
			case BEAM_STANDARD:
				AddBeamSurfaces(b);
				break;
			case BEAM_STAR:
				AddStarBeam(b);
				break;
			case BEAM_CYLINDER:
				AddCylinderSurfaces(b);
				break;
			}
		}
	}

	unguard;
}


//?? rename: VisSetup() or MarkNodes() (more correct, than MarkLeaves) or MarkBsp() ...
//?? NOTE: this function is slow in comparision with whole frontend
static void MarkLeaves()
{
	int		i;
	CBspNode *n;
	CBspLeaf *l;

	// determine the vieworg cluster
	int cluster = CM_FindLeaf(vp.view.origin)->cluster;
	// if cluster or zonemask changed -- re-mark visible leaves
	if (viewCluster == cluster && !forceVisMap) return;

	viewCluster = cluster;
	forceVisMap = false;
	visFrame++;
	if (r_novis->integer || cluster < 0 || !bspfile.visInfo || cluster >= bspfile.numClusters || bspfile.numClusters <= 1)
	{
		// mark all nodes and all leafs
		for (i = 0, n = bspfile.nodes; i < bspfile.numNodes; i++, n++)
			n->ex->visFrame = visFrame;
		for (i = 0, l = bspfile.leafs; i < bspfile.numLeafs; i++, l++)
			l->ex->visFrame = visFrame;
		STAT(gl_stats.visLeafs = bspfile.numLeafs);
		return;
	}

	// use visinfo to mark nodes
	STAT(gl_stats.visLeafs = 0);
	const byte *row = CM_ClusterPVS(cluster);
	for (i = 0, l = bspfile.leafs; i < bspfile.numLeafs; i++, l++)
	{
		int cl = l->cluster;
		int ar = l->zone;
#define TEST_BIT(arr,bit)	((arr[bit>>3] >> (bit&7)) & 1)
		if (TEST_BIT(row,cl) && TEST_BIT(zoneMask,ar))
		{
			// mark leaf
			l->ex->visFrame = visFrame;
			// mark nodes
			for (CBspNode *p = l->parent; p && p->ex->visFrame != visFrame; p = p->parent)
				p->ex->visFrame = visFrame;
			STAT(gl_stats.visLeafs++);
		}
	}
}


void AddEntity(entity_t *ent)
{
	if (gl_numEntities >= MAX_GLENTITIES)
	{
		appWPrintf("R_AddEntity: MAX_GLENTITIES hit\n");
		return;
	}

	bool mirror = (ent->flags & RF_MIRROR) != 0;

	refEntity_t *out = &gl_entities[gl_numEntities];
	memset(out, 0, sizeof(refEntity_t));

	// common fields
	out->flags = ent->flags;
	out->model = static_cast<model_t*>(ent->model);		// namespace conversion (:: -> OpenGLDrv)
	out->shaderColor.rgba = ent->color.rgba;
	out->time  = vp.time - ent->time;
	if (out->time < 0)
	{
		DrawTextLeft(va("ent.time > time for %s\n", ent->model ? *ent->model->Name : "(no model)"));
		out->time = 0;
	}

	out->coord = ent->pos;
	if (ent->model)
	{
#if 0
		-- does not works: vec=={1,0,0} may be represented in memory as {1,0,-0} (-0 is 0x80000000)
		-- note: fast FNegate(0) => -0 is not a reason of this!
		if (!memcmp(&ent->pos.origin, &nullVec3, sizeof(CVec3)) &&
			!memcmp(&ent->pos.axis, &identAxis, sizeof(CAxis)))
			out->worldMatrix = true;
		else
			out->worldMatrix = false;
		// dump for debug:
		DrawTextLeft(va("%s: %g %g %g : %g %g %g - %g %g %g - %g %g %g",*ent->model->Name,VECTOR_ARG(ent->pos.origin),
		VECTOR_ARG(ent->pos.axis[0]),VECTOR_ARG(ent->pos.axis[1]),VECTOR_ARG(ent->pos.axis[2])),RGB(1,1,0));
#define D(n) \
	DrawTextLeft(va("%08X %08X %08X",uint_cast_const(n[0]),uint_cast_const(n[1]),uint_cast_const(n[2])));
		if (ent->model->Name[0] == '*' && !strcmp(ent->model->Name,"*223"))
		{
			D(ent->pos.origin);
			D(nullVec3);
			D(ent->pos.axis[0]);
			D(identAxis[0]);
			D(ent->pos.axis[1]);
			D(identAxis[1]);
			D(ent->pos.axis[2]);
			D(identAxis[2]);
		}
#undef D
#else
		out->worldMatrix = (ent->pos.origin[0] == 0 && ent->pos.origin[1] == 0 && ent->pos.origin[2] == 0 &&
							ent->pos.axis[0][0] == 1 && ent->pos.axis[1][1] == 1 && ent->pos.axis[2][2] == 1);
#endif
		out->drawScale = (ent->scale) ? ent->scale : 1.0f;	// 0 == 1 == original size
		out->mirror    = mirror;
		out->frame     = ent->frame;
		out->oldFrame  = ent->oldframe;
		out->backLerp  = ent->backlerp;

		out->customShader = static_cast<shader_t*>(ent->customShader);
		out->skin      = ent->skin;
		out->skinNum   = ent->skinnum;				//?? check skinnum in [0..model.numSkins-1]
		out->shaderColor.c[3] = appRound(ent->alpha * 255);	//?? use color.c[3]

		// model-specific code and calculate model center
		if (!out->model->InitEntity(ent, out))
			return;
	}
	else if (ent->flags & RF_BBOX)
		out->size2 = ent->size;

	gl_numEntities++;
	vp.numEntities++;
	STAT(gl_stats.ents++);
}


void AddDlight(dlight_t *dl)
{
	if (gl_numDlights >= MAX_GLDLIGHTS)
	{
		appWPrintf("R_AddDlight: MAX_GLDLIGHTS hit\n");
		return;
	}
	if (!dl->intensity) return;

	refDlight_t *out = &gl_dlights[gl_numDlights++];
	out->origin    = dl->origin;
	out->intensity = dl->intensity;
	float r = dl->color[0] * gl_config.identityLightValue;
	float g = dl->color[1] * gl_config.identityLightValue;
	float b = dl->color[2] * gl_config.identityLightValue;
	float sat = r_saturation->value;
	if (sat != 1.0f)
	{
		float l = (r + g + b) / 3.0f;
		SATURATE(r,l,sat);
		SATURATE(g,l,sat);
		SATURATE(b,l,sat);
	}
	int r1 = appRound(r);
	int g1 = appRound(g);
	int b1 = appRound(b);
	if (!gl_config.doubleModulateLM)
	{
		r1 <<= 1;
		g1 <<= 1;
		b1 <<= 1;
		NORMALIZE_COLOR255(r1, g1, b1);
	}
	out->c.c[0] = r1;
	out->c.c[1] = g1;
	out->c.c[2] = b1;
	out->c.c[3] = 255;		// alpha
}



/*--------------------------------------------------------------*/


void DrawPortal()
{
	guardSlow(DrawPortal);
	int		i;
	refEntity_t *e;

	if (
#if !NO_DEBUG
		r_drawworld->integer &&
#endif
		!(vp.flags & RDF_NOWORLDMODEL))
	{
		drawFrame++;

		// setup world entity
		memset(&gl_entities[ENTITYNUM_WORLD], 0, sizeof(refEntity_t));
		gl_entities[ENTITYNUM_WORLD].modelvieworg = vp.view.origin;
		gl_entities[ENTITYNUM_WORLD].coord.axis   = identAxis;
		gl_entities[ENTITYNUM_WORLD].worldMatrix  = true;

		ClearSky();
		MarkLeaves();
		vp.bounds.Clear();
		const CBspLeaf *firstLeaf = WalkBspTree();
		DrawEntities(vp.firstEntity, vp.numEntities);
		DrawParticles();

		STAT(clock(gl_stats.walkBsp));
		DrawBspSequence(firstLeaf);
		STAT(unclock(gl_stats.walkBsp));
		if (gl_flares->integer && gl_flareShader)
			DrawFlares();
	}
	else
	{
		for (i = 0, e = gl_entities + vp.firstEntity; i < vp.numEntities; i++, e++)
		{
			if (!e->model) continue;

			SetupModelMatrix(e);

			currentEntity = e - gl_entities;
			if (e->model)
			{
				e->visible = true;
				e->model->AddSurfaces(e);
			}
		}
	}
	unguardSlow;
}


} // namespace
