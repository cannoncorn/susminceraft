/*
mapblock_mesh.cpp
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
*/

/*
This file is part of Freeminer.

Freeminer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Freeminer  is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Freeminer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mapblock_mesh.h"
#include "light.h"
#include "mapblock.h"
#include "map.h"
#include "profiler.h"
#include "nodedef.h"
#include "gamedef.h"
#include "mesh.h"
#include "minimap.h"
#include "content_mapblock.h"
#include "noise.h"
#include "shader.h"
#include "settings.h"
#include "util/directiontables.h"
#include "clientmap.h"
#include "log_types.h"
#include <IMeshManipulator.h>

static void applyFacesShading(video::SColor &color, const float factor)
{
	color.setRed(core::clamp(core::round32(color.getRed() * factor), 0, 255));
	color.setGreen(core::clamp(core::round32(color.getGreen() * factor), 0, 255));
}

int getFarmeshStep(MapDrawControl& draw_control, const v3POS & playerpos, const v3POS & blockpos) {
	int range = radius_box(playerpos, blockpos);
	if (draw_control.farmesh) {
		const POS nearest = 256/MAP_BLOCKSIZE;
		if		(range >= std::min<POS>(nearest*8, draw_control.farmesh+draw_control.farmesh_step*4))	return 16;
		else if (range >= std::min<POS>(nearest*4, draw_control.farmesh+draw_control.farmesh_step*2))	return 8;
		else if (range >= std::min<POS>(nearest*2, draw_control.farmesh+draw_control.farmesh_step))	return 4;
		else if (range >= std::min<POS>(nearest, draw_control.farmesh))								return 2;
	}
	return 1;
};

/*
	MeshMakeData
*/

MeshMakeData::MeshMakeData(IGameDef *gamedef, bool use_shaders,
		bool use_tangent_vertices,
		Map & map_, MapDrawControl& draw_control_):
#if defined(MESH_ZEROCOPY)
	m_vmanip(map_),
#endif
	m_blockpos(-1337,-1337,-1337),
	m_crack_pos_relative(-1337, -1337, -1337),
	m_smooth_lighting(false),
	m_show_hud(false),
	m_gamedef(gamedef),


	m_use_shaders(use_shaders),
	m_use_tangent_vertices(use_tangent_vertices)

	,
	step(1),
	range(1),
	no_draw(false),
	timestamp(0),
	block(nullptr),
	map(map_),
	draw_control(draw_control_),
	debug(0),
	filled(false)
{}

MeshMakeData::~MeshMakeData() {
	//infostream<<"~MeshMakeData "<<m_blockpos<<std::endl;
}

void MeshMakeData::fill(MapBlock *block_)
{
#if ! ENABLE_THREADS
	block = block_;
#endif
	m_blockpos = block_->getPos();
}

bool MeshMakeData::fill_data()
{

	if (filled)
		return filled;

	if (!block)
		block = map.getBlockNoCreateNoEx(m_blockpos);

	if (!block)
		return filled;
	filled = true;
	timestamp = block->getTimestamp();

#if !defined(MESH_ZEROCOPY)
	ScopeProfiler sp(g_profiler, "Client: Mesh data fill");

	map.copy_27_blocks_to_vm(block, m_vmanip);

#if 0
	v3POS blockpos_nodes = m_blockpos*MAP_BLOCKSIZE;

	/*
		Copy data
	*/

	// Allocate this block + neighbors
	m_vmanip.clear();
	VoxelArea voxel_area(blockpos_nodes - v3s16(1,1,1) * MAP_BLOCKSIZE,
			blockpos_nodes + v3s16(1,1,1) * MAP_BLOCKSIZE*2-v3s16(1,1,1));
	m_vmanip.addArea(voxel_area);

	{
		//TimeTaker timer("copy central block data");
		// 0ms

		// Copy our data
		block->copyTo(m_vmanip);
	}
	{
		//TimeTaker timer("copy neighbor block data");
		// 0ms

		/*
			Copy neighbors. This is lightning fast.
			Copying only the borders would be *very* slow.
		*/

		// Get map
		Map *map = block->getParent();

		for(u16 i=0; i<26; i++)
		{
			const v3s16 &dir = g_26dirs[i];
			v3s16 bp = m_blockpos + dir;
			MapBlock *b = map->getBlockNoCreateNoEx(bp);
			if(b)
				b->copyTo(m_vmanip);
		}
	}

#endif

#endif
	return filled;
}

void MeshMakeData::fillSingleNode(MapNode *node, v3POS blockpos) {
	m_blockpos = blockpos;

#if !defined(MESH_ZEROCOPY)
	v3s16 blockpos_nodes = m_blockpos * MAP_BLOCKSIZE;
	VoxelArea area(blockpos_nodes-v3s16(1,1,1)*MAP_BLOCKSIZE,
			blockpos_nodes+v3s16(1,1,1)*MAP_BLOCKSIZE*2-v3s16(1,1,1));
	s32 volume = area.getVolume();
	s32 our_node_index = area.index(1,1,1);

	// Allocate this block + neighbors
	m_vmanip.clear();
	m_vmanip.addArea(area);

	// Fill in data
	MapNode *data = reinterpret_cast<MapNode*>( ::operator new(volume * sizeof(MapNode)));
	for(s32 i = 0; i < volume; i++)
	{
		if(i == our_node_index)
		{
			data[i] = *node;
		}
		else
		{
			data[i] = MapNode(CONTENT_AIR, LIGHT_MAX, 0);
		}
	}
	m_vmanip.copyFrom(data, area, area.MinEdge, area.MinEdge, area.getExtent());
	delete data;
#endif
}

void MeshMakeData::setCrack(int crack_level, v3s16 crack_pos)
{
	if(crack_level >= 0)
		m_crack_pos_relative = crack_pos - m_blockpos*MAP_BLOCKSIZE;
}

void MeshMakeData::setSmoothLighting(bool smooth_lighting)
{
	m_smooth_lighting = smooth_lighting;
}

/*
	Light and vertex color functions
*/

/*
	Calculate non-smooth lighting at interior of node.
	Single light bank.
*/
static u8 getInteriorLight(enum LightBank bank, MapNode n, s32 increment,
		INodeDefManager *ndef)
{
	u8 light = n.getLight(bank, ndef);

	while(increment > 0)
	{
		light = undiminish_light(light);
		--increment;
	}
	while(increment < 0)
	{
		light = diminish_light(light);
		++increment;
	}

	return decode_light(light);
}

/*
	Calculate non-smooth lighting at interior of node.
	Both light banks.
*/
u16 getInteriorLight(MapNode n, s32 increment, INodeDefManager *ndef)
{
	u16 day = getInteriorLight(LIGHTBANK_DAY, n, increment, ndef);
	u16 night = getInteriorLight(LIGHTBANK_NIGHT, n, increment, ndef);
	return day | (night << 8);
}

/*
	Calculate non-smooth lighting at face of node.
	Single light bank.
*/
static u8 getFaceLight(enum LightBank bank, MapNode n, MapNode n2,
		v3s16 face_dir, INodeDefManager *ndef)
{
	u8 light;
	u8 l1 = n.getLight(bank, ndef);
	u8 l2 = n2.getLight(bank, ndef);
	if(l1 > l2)
		light = l1;
	else
		light = l2;

	// Boost light level for light sources
	u8 light_source = MYMAX(ndef->get(n).light_source,
			ndef->get(n2).light_source);
	if(light_source > light)
		light = light_source;

	return decode_light(light);
}

/*
	Calculate non-smooth lighting at face of node.
	Both light banks.
*/
u16 getFaceLight(MapNode n, MapNode n2, v3s16 face_dir, INodeDefManager *ndef)
{
	u16 day = getFaceLight(LIGHTBANK_DAY, n, n2, face_dir, ndef);
	u16 night = getFaceLight(LIGHTBANK_NIGHT, n, n2, face_dir, ndef);
	return day | (night << 8);
}

/*
	Calculate smooth lighting at the XYZ- corner of p.
	Both light banks
*/
static u16 getSmoothLightCombined(v3s16 p, MeshMakeData *data)
{
	static const v3s16 dirs8[8] = {
		v3s16(0,0,0),
		v3s16(0,0,1),
		v3s16(0,1,0),
		v3s16(0,1,1),
		v3s16(1,0,0),
		v3s16(1,1,0),
		v3s16(1,0,1),
		v3s16(1,1,1),
	};

	INodeDefManager *ndef = data->m_gamedef->ndef();

	u16 ambient_occlusion = 0;
	u16 light_count = 0;
	u8 light_source_max = 0;
	u16 light_day = 0;
	u16 light_night = 0;

	for (u32 i = 0; i < 8; i++)
	{
		const MapNode &n = data->m_vmanip.getNodeRefUnsafeCheckFlags(p - dirs8[i]);

		// if it's CONTENT_IGNORE we can't do any light calculations
		if (n.getContent() == CONTENT_IGNORE) {
			continue;
		}

		const ContentFeatures &f = ndef->get(n);
		if (f.light_source > light_source_max)
			light_source_max = f.light_source;
		// Check f.solidness because fast-style leaves look better this way
		if (f.param_type == CPT_LIGHT && f.solidness != 2) {
			light_day += decode_light(n.getLightNoChecks(LIGHTBANK_DAY, &f));
			light_night += decode_light(n.getLightNoChecks(LIGHTBANK_NIGHT, &f));
			light_count++;
		} else {
			ambient_occlusion++;
		}
	}

	if(light_count == 0)
		return 0xffff;

	light_day /= light_count;
	light_night /= light_count;

	// Boost brightness around light sources
	bool skip_ambient_occlusion_day = false;
	if(decode_light(light_source_max) >= light_day) {
		light_day = decode_light(light_source_max);
		skip_ambient_occlusion_day = true;
	}

	bool skip_ambient_occlusion_night = false;
	if(decode_light(light_source_max) >= light_night) {
		light_night = decode_light(light_source_max);
		skip_ambient_occlusion_night = true;
	}

	if (ambient_occlusion > 4)
	{
		static const float ao_gamma = rangelim(
			g_settings->getFloat("ambient_occlusion_gamma"), 0.25, 4.0);

		// Table of gamma space multiply factors.
		static const float light_amount[3] = {
			powf(0.75, 1.0 / ao_gamma),
			powf(0.5,  1.0 / ao_gamma),
			powf(0.25, 1.0 / ao_gamma)
		};

		//calculate table index for gamma space multiplier
		ambient_occlusion -= 5;

		if (!skip_ambient_occlusion_day)
			light_day = rangelim(core::round32(light_day*light_amount[ambient_occlusion]), 0, 255);
		if (!skip_ambient_occlusion_night)
			light_night = rangelim(core::round32(light_night*light_amount[ambient_occlusion]), 0, 255);
	}

	return light_day | (light_night << 8);
}

/*
	Calculate smooth lighting at the given corner of p.
	Both light banks.
*/
u16 getSmoothLight(v3s16 p, v3s16 corner, MeshMakeData *data)
{
	if(corner.X == 1) p.X += 1;
	// else corner.X == -1
	if(corner.Y == 1) p.Y += 1;
	// else corner.Y == -1
	if(corner.Z == 1) p.Z += 1;
	// else corner.Z == -1

	return getSmoothLightCombined(p, data);
}

/*
	Converts from day + night color values (0..255)
	and a given daynight_ratio to the final SColor shown on screen.
*/
void finalColorBlend(video::SColor& result,
		u8 day, u8 night, u32 daynight_ratio)
{
	s32 rg = (day * daynight_ratio + night * (1000-daynight_ratio)) / 1000;
	s32 b = rg;

	// Moonlight is blue
	b += (day - night) / 13;
	rg -= (day - night) / 23;

	// Emphase blue a bit in darker places
	// Each entry of this array represents a range of 8 blue levels
	static const u8 emphase_blue_when_dark[35] = {
		1, 4, 6, 6, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0
	};
	b += emphase_blue_when_dark[irr::core::clamp(b, 0, 255) / 8];
	b = irr::core::clamp(b, 0, 255);

	// Artificial light is yellow-ish
	static const u8 emphase_yellow_when_artificial[16] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 10, 15, 15, 15
	};
	rg += emphase_yellow_when_artificial[night/16];
	rg = irr::core::clamp(rg, 0, 255);

	result.setRed(rg);
	result.setGreen(rg);
	result.setBlue(b);
}

/*
	Mesh generation helpers
*/

/*
	vertex_dirs: v3s16[4]
*/
static void getNodeVertexDirs(v3s16 dir, v3s16 *vertex_dirs)
{
	/*
		If looked from outside the node towards the face, the corners are:
		0: bottom-right
		1: bottom-left
		2: top-left
		3: top-right
	*/
	if(dir == v3s16(0,0,1))
	{
		// If looking towards z+, this is the face that is behind
		// the center point, facing towards z+.
		vertex_dirs[0] = v3s16(-1,-1, 1);
		vertex_dirs[1] = v3s16( 1,-1, 1);
		vertex_dirs[2] = v3s16( 1, 1, 1);
		vertex_dirs[3] = v3s16(-1, 1, 1);
	}
	else if(dir == v3s16(0,0,-1))
	{
		// faces towards Z-
		vertex_dirs[0] = v3s16( 1,-1,-1);
		vertex_dirs[1] = v3s16(-1,-1,-1);
		vertex_dirs[2] = v3s16(-1, 1,-1);
		vertex_dirs[3] = v3s16( 1, 1,-1);
	}
	else if(dir == v3s16(1,0,0))
	{
		// faces towards X+
		vertex_dirs[0] = v3s16( 1,-1, 1);
		vertex_dirs[1] = v3s16( 1,-1,-1);
		vertex_dirs[2] = v3s16( 1, 1,-1);
		vertex_dirs[3] = v3s16( 1, 1, 1);
	}
	else if(dir == v3s16(-1,0,0))
	{
		// faces towards X-
		vertex_dirs[0] = v3s16(-1,-1,-1);
		vertex_dirs[1] = v3s16(-1,-1, 1);
		vertex_dirs[2] = v3s16(-1, 1, 1);
		vertex_dirs[3] = v3s16(-1, 1,-1);
	}
	else if(dir == v3s16(0,1,0))
	{
		// faces towards Y+ (assume Z- as "down" in texture)
		vertex_dirs[0] = v3s16( 1, 1,-1);
		vertex_dirs[1] = v3s16(-1, 1,-1);
		vertex_dirs[2] = v3s16(-1, 1, 1);
		vertex_dirs[3] = v3s16( 1, 1, 1);
	}
	else if(dir == v3s16(0,-1,0))
	{
		// faces towards Y- (assume Z+ as "down" in texture)
		vertex_dirs[0] = v3s16( 1,-1, 1);
		vertex_dirs[1] = v3s16(-1,-1, 1);
		vertex_dirs[2] = v3s16(-1,-1,-1);
		vertex_dirs[3] = v3s16( 1,-1,-1);
	}
}

struct FastFace
{
	TileSpec tile;
	video::S3DVertex vertices[4]; // Precalculated vertices
};

static void makeFastFace(TileSpec tile, u16 li0, u16 li1, u16 li2, u16 li3,
		v3f p, v3s16 dir, v3f scale, u8 light_source, std::vector<FastFace> &dest)
{
	// Position is at the center of the cube.
	v3f pos = p * BS;

	float x0 = 0.0;
	float y0 = 0.0;
	float w = 1.0;
	float h = 1.0;

	v3f vertex_pos[4];
	v3s16 vertex_dirs[4];
	getNodeVertexDirs(dir, vertex_dirs);

	v3s16 t;
	u16 t1;
	switch (tile.rotation)
	{
	case 0:
		break;
	case 1: //R90
		t = vertex_dirs[0];
		vertex_dirs[0] = vertex_dirs[3];
		vertex_dirs[3] = vertex_dirs[2];
		vertex_dirs[2] = vertex_dirs[1];
		vertex_dirs[1] = t;
		t1=li0;
		li0=li3;
		li3=li2;
		li2=li1;
		li1=t1;
		break;
	case 2: //R180
		t = vertex_dirs[0];
		vertex_dirs[0] = vertex_dirs[2];
		vertex_dirs[2] = t;
		t = vertex_dirs[1];
		vertex_dirs[1] = vertex_dirs[3];
		vertex_dirs[3] = t;
		t1  = li0;
		li0 = li2;
		li2 = t1;
		t1  = li1;
		li1 = li3;
		li3 = t1;
		break;
	case 3: //R270
		t = vertex_dirs[0];
		vertex_dirs[0] = vertex_dirs[1];
		vertex_dirs[1] = vertex_dirs[2];
		vertex_dirs[2] = vertex_dirs[3];
		vertex_dirs[3] = t;
		t1  = li0;
		li0 = li1;
		li1 = li2;
		li2 = li3;
		li3 = t1;
		break;
	case 4: //FXR90
		t = vertex_dirs[0];
		vertex_dirs[0] = vertex_dirs[3];
		vertex_dirs[3] = vertex_dirs[2];
		vertex_dirs[2] = vertex_dirs[1];
		vertex_dirs[1] = t;
		t1  = li0;
		li0 = li3;
		li3 = li2;
		li2 = li1;
		li1 = t1;
		y0 += h;
		h *= -1;
		break;
	case 5: //FXR270
		t = vertex_dirs[0];
		vertex_dirs[0] = vertex_dirs[1];
		vertex_dirs[1] = vertex_dirs[2];
		vertex_dirs[2] = vertex_dirs[3];
		vertex_dirs[3] = t;
		t1  = li0;
		li0 = li1;
		li1 = li2;
		li2 = li3;
		li3 = t1;
		y0 += h;
		h *= -1;
		break;
	case 6: //FYR90
		t = vertex_dirs[0];
		vertex_dirs[0] = vertex_dirs[3];
		vertex_dirs[3] = vertex_dirs[2];
		vertex_dirs[2] = vertex_dirs[1];
		vertex_dirs[1] = t;
		t1  = li0;
		li0 = li3;
		li3 = li2;
		li2 = li1;
		li1 = t1;
		x0 += w;
		w *= -1;
		break;
	case 7: //FYR270
		t = vertex_dirs[0];
		vertex_dirs[0] = vertex_dirs[1];
		vertex_dirs[1] = vertex_dirs[2];
		vertex_dirs[2] = vertex_dirs[3];
		vertex_dirs[3] = t;
		t1  = li0;
		li0 = li1;
		li1 = li2;
		li2 = li3;
		li3 = t1;
		x0 += w;
		w *= -1;
		break;
	case 8: //FX
		y0 += h;
		h *= -1;
		break;
	case 9: //FY
		x0 += w;
		w *= -1;
		break;
	default:
		break;
	}

	for(u16 i=0; i<4; i++)
	{
		vertex_pos[i] = v3f(
				BS/2*vertex_dirs[i].X,
				BS/2*vertex_dirs[i].Y,
				BS/2*vertex_dirs[i].Z
		);
	}

	for(u16 i=0; i<4; i++)
	{
		vertex_pos[i].X *= scale.X;
		vertex_pos[i].Y *= scale.Y;
		vertex_pos[i].Z *= scale.Z;
		vertex_pos[i] += pos;
	}

	f32 abs_scale = 1.0;
	if     (scale.X < 0.999 || scale.X > 1.001) abs_scale = scale.X;
	else if(scale.Y < 0.999 || scale.Y > 1.001) abs_scale = scale.Y;
	else if(scale.Z < 0.999 || scale.Z > 1.001) abs_scale = scale.Z;

	v3f normal(dir.X, dir.Y, dir.Z);

	u8 alpha = tile.alpha;

	dest.push_back(FastFace());

	FastFace& face = *dest.rbegin();

	face.vertices[0] = video::S3DVertex(vertex_pos[0], normal,
			MapBlock_LightColor(alpha, li0, light_source),
			core::vector2d<f32>(x0+w*abs_scale, y0+h));
	face.vertices[1] = video::S3DVertex(vertex_pos[1], normal,
			MapBlock_LightColor(alpha, li1, light_source),
			core::vector2d<f32>(x0, y0+h));
	face.vertices[2] = video::S3DVertex(vertex_pos[2], normal,
			MapBlock_LightColor(alpha, li2, light_source),
			core::vector2d<f32>(x0, y0));
	face.vertices[3] = video::S3DVertex(vertex_pos[3], normal,
			MapBlock_LightColor(alpha, li3, light_source),
			core::vector2d<f32>(x0+w*abs_scale, y0));

	face.tile = tile;
}

/*
	Nodes make a face if contents differ and solidness differs.
	Return value:
		0: No face
		1: Face uses m1's content
		2: Face uses m2's content
	equivalent: Whether the blocks share the same face (eg. water and glass)

	TODO: Add 3: Both faces drawn with backface culling, remove equivalent
*/
static u8 face_contents(content_t m1, content_t m2, bool *equivalent,
		INodeDefManager *ndef, int step)
{
	*equivalent = false;

	bool have_ignore = (m1 == CONTENT_IGNORE || m2 == CONTENT_IGNORE);
	if(step <= 1 && have_ignore)
		return 0;

	bool contents_differ = (m1 != m2);

	const ContentFeatures &f1 = ndef->get(m1);
	const ContentFeatures &f2 = ndef->get(m2);

	// Contents don't differ for different forms of same liquid
	if(f1.sameLiquid(f2))
		contents_differ = false;

	u8 c1 = f1.solidness;
	u8 c2 = f2.solidness;

	if (step > 1) {
		//no liquid/transparent borders
		if (have_ignore && c1 == 1)
			c1 = 0;
		if (have_ignore && c2 == 1)
			c2 = 0;
		if (!c1)
			c1 = f1.solidness_far;
		if (!c2)
			c2 = f2.solidness_far;
	}

	bool solidness_differs = (c1 != c2);
	bool makes_face = contents_differ && solidness_differs;

	if(makes_face == false)
		return 0;

	if(c1 == 0)
		c1 = f1.visual_solidness;
	if(c2 == 0)
		c2 = f2.visual_solidness;

	if(c1 == c2){
		*equivalent = true;
		// If same solidness, liquid takes precense
		if(f1.isLiquid())
			return 1;
		if(f2.isLiquid())
			return 2;
	}

	if(c1 > c2)
		return 1;
	else
		return 2;
}

/*
	Gets nth node tile (0 <= n <= 5).
*/
TileSpec getNodeTileN(MapNode mn, v3s16 p, u8 tileindex, MeshMakeData *data)
{
	INodeDefManager *ndef = data->m_gamedef->ndef();
	TileSpec spec = ndef->get(mn).tiles[tileindex];
	// Apply temporary crack
	if (p == data->m_crack_pos_relative)
		spec.material_flags |= MATERIAL_FLAG_CRACK;
	return spec;
}

/*
	Gets node tile given a face direction.
*/
TileSpec getNodeTile(MapNode mn, v3s16 p, v3s16 dir, MeshMakeData *data)
{
	INodeDefManager *ndef = data->m_gamedef->ndef();

	// Direction must be (1,0,0), (-1,0,0), (0,1,0), (0,-1,0),
	// (0,0,1), (0,0,-1) or (0,0,0)
	assert(dir.X * dir.X + dir.Y * dir.Y + dir.Z * dir.Z <= 1);

	// Convert direction to single integer for table lookup
	//  0 = (0,0,0)
	//  1 = (1,0,0)
	//  2 = (0,1,0)
	//  3 = (0,0,1)
	//  4 = invalid, treat as (0,0,0)
	//  5 = (0,0,-1)
	//  6 = (0,-1,0)
	//  7 = (-1,0,0)
	u8 dir_i = ((dir.X + 2 * dir.Y + 3 * dir.Z) & 7)*2;

	// Get rotation for things like chests
	u8 facedir = mn.getFaceDir(ndef);

	static const u16 dir_to_tile[24 * 16] =
	{
		// 0     +X    +Y    +Z           -Z    -Y    -X   ->   value=tile,rotation
		   0,0,  2,0 , 0,0 , 4,0 ,  0,0,  5,0 , 1,0 , 3,0 ,  // rotate around y+ 0 - 3
		   0,0,  4,0 , 0,3 , 3,0 ,  0,0,  2,0 , 1,1 , 5,0 ,
		   0,0,  3,0 , 0,2 , 5,0 ,  0,0,  4,0 , 1,2 , 2,0 ,
		   0,0,  5,0 , 0,1 , 2,0 ,  0,0,  3,0 , 1,3 , 4,0 ,

		   0,0,  2,3 , 5,0 , 0,2 ,  0,0,  1,0 , 4,2 , 3,1 ,  // rotate around z+ 4 - 7
		   0,0,  4,3 , 2,0 , 0,1 ,  0,0,  1,1 , 3,2 , 5,1 ,
		   0,0,  3,3 , 4,0 , 0,0 ,  0,0,  1,2 , 5,2 , 2,1 ,
		   0,0,  5,3 , 3,0 , 0,3 ,  0,0,  1,3 , 2,2 , 4,1 ,

		   0,0,  2,1 , 4,2 , 1,2 ,  0,0,  0,0 , 5,0 , 3,3 ,  // rotate around z- 8 - 11
		   0,0,  4,1 , 3,2 , 1,3 ,  0,0,  0,3 , 2,0 , 5,3 ,
		   0,0,  3,1 , 5,2 , 1,0 ,  0,0,  0,2 , 4,0 , 2,3 ,
		   0,0,  5,1 , 2,2 , 1,1 ,  0,0,  0,1 , 3,0 , 4,3 ,

		   0,0,  0,3 , 3,3 , 4,1 ,  0,0,  5,3 , 2,3 , 1,3 ,  // rotate around x+ 12 - 15
		   0,0,  0,2 , 5,3 , 3,1 ,  0,0,  2,3 , 4,3 , 1,0 ,
		   0,0,  0,1 , 2,3 , 5,1 ,  0,0,  4,3 , 3,3 , 1,1 ,
		   0,0,  0,0 , 4,3 , 2,1 ,  0,0,  3,3 , 5,3 , 1,2 ,

		   0,0,  1,1 , 2,1 , 4,3 ,  0,0,  5,1 , 3,1 , 0,1 ,  // rotate around x- 16 - 19
		   0,0,  1,2 , 4,1 , 3,3 ,  0,0,  2,1 , 5,1 , 0,0 ,
		   0,0,  1,3 , 3,1 , 5,3 ,  0,0,  4,1 , 2,1 , 0,3 ,
		   0,0,  1,0 , 5,1 , 2,3 ,  0,0,  3,1 , 4,1 , 0,2 ,

		   0,0,  3,2 , 1,2 , 4,2 ,  0,0,  5,2 , 0,2 , 2,2 ,  // rotate around y- 20 - 23
		   0,0,  5,2 , 1,3 , 3,2 ,  0,0,  2,2 , 0,1 , 4,2 ,
		   0,0,  2,2 , 1,0 , 5,2 ,  0,0,  4,2 , 0,0 , 3,2 ,
		   0,0,  4,2 , 1,1 , 2,2 ,  0,0,  3,2 , 0,3 , 5,2

	};
	u16 tile_index=facedir*16 + dir_i;
	TileSpec spec = getNodeTileN(mn, p, dir_to_tile[tile_index], data);
	spec.rotation=dir_to_tile[tile_index + 1];
	spec.texture = data->m_gamedef->tsrc()->getTexture(spec.texture_id);
	return spec;
}

static void getTileInfo(
		// Input:
		MeshMakeData *data,
		const v3s16 &p,
		const v3s16 &face_dir,
		// Output:
		bool &makes_face,
		v3s16 &p_corrected,
		v3s16 &face_dir_corrected,
		u16 *lights,
		TileSpec &tile,
		u8 &light_source
		,int step
	)
{
	auto &vmanip = data->m_vmanip;
	INodeDefManager *ndef = data->m_gamedef->ndef();
	v3s16 blockpos_nodes = data->m_blockpos * MAP_BLOCKSIZE;

	MapNode n0;
	for(int find = 0; find < step; ++find) {
		n0 = vmanip.getNodeRefUnsafe(blockpos_nodes + p*step + find);
		if (step <= 1 || (n0.getContent() != CONTENT_IGNORE && n0.getContent() != CONTENT_AIR))
			break;
	}

	// Don't even try to get n1 if n0 is already CONTENT_IGNORE
	if (step <= 1 && n0.getContent() == CONTENT_IGNORE) {
		makes_face = false;
		return;
	}

	MapNode n1;
	for(int find = 0; find < step; ++find) {
		n1 = vmanip.getNodeRefUnsafeCheckFlags(blockpos_nodes + p*step + face_dir*step + find);
		if (step <= 1 || (n1.getContent() != CONTENT_IGNORE && n1.getContent() != CONTENT_AIR))
			break;
	}
	// if(data->debug) infostream<<" GN "<<n0<< n1<< blockpos_nodes<<blockpos_nodes + p*step<<blockpos_nodes + p*step + face_dir*step<<std::endl;

	if (step <= 1 && n1.getContent() == CONTENT_IGNORE) {
		makes_face = false;
		return;
	}

	// This is hackish
	bool equivalent = false;
	u8 mf = face_contents(n0.getContent(), n1.getContent(),
			&equivalent, ndef, step);

	if(mf == 0)
	{
		makes_face = false;
		return;
	}

	makes_face = true;

	if(mf == 1)
	{
		tile = getNodeTile(n0, p, face_dir, data);
		p_corrected = p;
		face_dir_corrected = face_dir;
		light_source = ndef->get(n0).light_source;
	}
	else
	{
		tile = getNodeTile(n1, p + face_dir, -face_dir, data);
		p_corrected = p + face_dir;
		face_dir_corrected = -face_dir;
		light_source = ndef->get(n1).light_source;
	}

	// eg. water and glass
	if(equivalent)
		tile.material_flags |= MATERIAL_FLAG_BACKFACE_CULLING;

	if(data->m_smooth_lighting == false || step > 1)
	{
		if (step > 1 && (!n0.getContent() || !n1.getContent()))
			lights[0] = lights[1] = lights[2] = lights[3] = decode_light(LIGHT_MAX-2);
		else
		lights[0] = lights[1] = lights[2] = lights[3] =
				getFaceLight(n0, n1, face_dir, ndef);
	}
	else
	{
		v3s16 vertex_dirs[4];
		getNodeVertexDirs(face_dir_corrected, vertex_dirs);
		for(u16 i=0; i<4; i++)
		{
			lights[i] = getSmoothLight(
					blockpos_nodes + p_corrected,
					vertex_dirs[i], data);
		}
	}

	return;
}

/*
	startpos:
	translate_dir: unit vector with only one of x, y or z
	face_dir: unit vector with only one of x, y or z
*/
static void updateFastFaceRow(
		MeshMakeData *data,
		v3s16 startpos,
		v3s16 translate_dir,
		v3f translate_dir_f,
		v3s16 face_dir,
		v3f face_dir_f,
		std::vector<FastFace> &dest,
		int step)
{
	v3s16 p = startpos;

	u16 continuous_tiles_count = 1;

	bool makes_face = false;
	v3s16 p_corrected;
	v3s16 face_dir_corrected;
	u16 lights[4] = {0,0,0,0};
	TileSpec tile;
	u8 light_source = 0;
	getTileInfo(data, p, face_dir,
			makes_face, p_corrected, face_dir_corrected,
			lights, tile, light_source, step);

	auto prev_p_corrected = p_corrected;

	u16 to = MAP_BLOCKSIZE/step;
	for(u16 j=0; j<to; j++)
	{
		// If tiling can be done, this is set to false in the next step
		bool next_is_different = true;

		v3s16 p_next;

		bool next_makes_face = false;
		v3s16 next_p_corrected;
		v3s16 next_face_dir_corrected;
		u16 next_lights[4] = {0,0,0,0};
		TileSpec next_tile;
		u8 next_light_source = 0;

		// If at last position, there is nothing to compare to and
		// the face must be drawn anyway
		if(j != to - 1)
		{
			p_next = p + translate_dir;

			getTileInfo(data, p_next, face_dir,
					next_makes_face, next_p_corrected,
					next_face_dir_corrected, next_lights,
					next_tile, next_light_source, step);

			if(next_makes_face == makes_face
					&& next_p_corrected == prev_p_corrected + translate_dir
					&& next_face_dir_corrected == face_dir_corrected
					&& next_lights[0] == lights[0]
					&& next_lights[1] == lights[1]
					&& next_lights[2] == lights[2]
					&& next_lights[3] == lights[3]
					&& next_tile == tile
					&& tile.rotation == 0
					&& next_light_source == light_source
					&& (tile.material_flags & MATERIAL_FLAG_TILEABLE_HORIZONTAL)
					&& (tile.material_flags & MATERIAL_FLAG_TILEABLE_VERTICAL)) {
				next_is_different = false;
				continuous_tiles_count++;
			} else {
				/*if(makes_face){
					g_profiler->add("Meshgen: diff: next_makes_face != makes_face",
							next_makes_face != makes_face ? 1 : 0);
					g_profiler->add("Meshgen: diff: n_p_corr != p_corr + t_dir",
							(next_p_corrected != p_corrected + translate_dir) ? 1 : 0);
					g_profiler->add("Meshgen: diff: next_f_dir_corr != f_dir_corr",
							next_face_dir_corrected != face_dir_corrected ? 1 : 0);
					g_profiler->add("Meshgen: diff: next_lights[] != lights[]",
							(next_lights[0] != lights[0] ||
							next_lights[0] != lights[0] ||
							next_lights[0] != lights[0] ||
							next_lights[0] != lights[0]) ? 1 : 0);
					g_profiler->add("Meshgen: diff: !(next_tile == tile)",
							!(next_tile == tile) ? 1 : 0);
				}*/
			}
			/*g_profiler->add("Meshgen: Total faces checked", 1);
			if(makes_face)
				g_profiler->add("Meshgen: Total makes_face checked", 1);*/
		} else {
			/*if(makes_face)
				g_profiler->add("Meshgen: diff: last position", 1);*/
		}

		if(next_is_different)
		{
			/*
				Create a face if there should be one
			*/
			if(makes_face)
			{
				// Floating point conversion of the position vector
				v3f pf(p_corrected.X, p_corrected.Y, p_corrected.Z);
				// Center point of face (kind of)
				v3f sp = pf - ((f32)continuous_tiles_count / 2.0 - 0.5) * translate_dir_f;
//?				if(continuous_tiles_count > 1)
//?					sp += translate_dir_f * (continuous_tiles_count - 1);
				v3f scale(1,1,1);

				if(translate_dir.X != 0) {
					scale.X = continuous_tiles_count;
				}
				if(translate_dir.Y != 0) {
					scale.Y = continuous_tiles_count;
				}
				if(translate_dir.Z != 0) {
					scale.Z = continuous_tiles_count;
				}

				makeFastFace(tile, lights[0], lights[1], lights[2], lights[3],
						sp, face_dir_corrected, scale, light_source,
						dest);

#if !defined(NDEBUG)
				g_profiler->avg("Meshgen: faces drawn by tiling", continuous_tiles_count);
#endif
			}

			continuous_tiles_count = 1;
		}

		makes_face = next_makes_face;
		p_corrected = next_p_corrected;
		face_dir_corrected = next_face_dir_corrected;
		lights[0] = next_lights[0];
		lights[1] = next_lights[1];
		lights[2] = next_lights[2];
		lights[3] = next_lights[3];
		tile = next_tile;
		light_source = next_light_source;
		p = p_next;
		prev_p_corrected = next_p_corrected;
	}
}

static void updateAllFastFaceRows(MeshMakeData *data,
		std::vector<FastFace> &dest, int step)
{
	s16 to = MAP_BLOCKSIZE/step;
	/*
		Go through every y,z and get top(y+) faces in rows of x+
	*/
	for(s16 y = 0; y < to; y++) {
		for(s16 z = 0; z < to; z++) {
			updateFastFaceRow(data,
					v3s16(0,y,z),
					v3s16(1,0,0), //dir
					v3f  (1,0,0),
					v3s16(0,1,0), //face dir
					v3f  (0,1,0),
					dest, step);
		}
	}

	/*
		Go through every x,y and get right(x+) faces in rows of z+
	*/
	for(s16 x = 0; x < to; x++) {
		for(s16 y = 0; y < to; y++) {
			updateFastFaceRow(data,
					v3s16(x,y,0),
					v3s16(0,0,1), //dir
					v3f  (0,0,1),
					v3s16(1,0,0), //face dir
					v3f  (1,0,0),
					dest, step);
		}
	}

	/*
		Go through every y,z and get back(z+) faces in rows of x+
	*/
	for(s16 z = 0; z < to; z++) {
		for(s16 y = 0; y < to; y++) {
			updateFastFaceRow(data,
					v3s16(0,y,z),
					v3s16(1,0,0), //dir
					v3f  (1,0,0),
					v3s16(0,0,1), //face dir
					v3f  (0,0,1),
					dest, step);
		}
	}
}

/*
	MapBlockMesh
*/

MapBlockMesh::MapBlockMesh(MeshMakeData *data, v3s16 camera_offset):
	step(data->step),
	no_draw(data->no_draw),
	m_mesh(nullptr),
	m_minimap_mapblock(NULL),
	m_gamedef(data->m_gamedef),
	m_driver(m_gamedef->tsrc()->getDevice()->getVideoDriver()),
	m_tsrc(m_gamedef->getTextureSource()),
	m_shdrsrc(m_gamedef->getShaderSource()),
	m_animation_force_timer(0), // force initial animation
	m_last_crack(-1),
	m_crack_materials(),
	m_last_daynight_ratio((u32) -1),
	m_daynight_diffs(),
	m_usage_timer(0)
{
	m_mesh = new scene::SMesh();

	m_enable_shaders = data->m_use_shaders;
	m_use_tangent_vertices = data->m_use_tangent_vertices;
	m_enable_vbo = g_settings->getBool("enable_vbo");

	if (!data->fill_data())
		return;
	if (step == 1 || !data->block->getMesh())
	if (g_settings->getBool("enable_minimap")) {
		m_minimap_mapblock = new MinimapMapblock;
		m_minimap_mapblock->getMinimapNodes(
			&data->m_vmanip, data->m_blockpos * MAP_BLOCKSIZE);
	}

	// 4-21ms for MAP_BLOCKSIZE=16  (NOTE: probably outdated)
	// 24-155ms for MAP_BLOCKSIZE=32  (NOTE: probably outdated)
	//TimeTaker timer1("MapBlockMesh()");


	timestamp = data->timestamp;

	std::vector<FastFace> fastfaces_new;
	fastfaces_new.reserve(512/step);

	/*
		We are including the faces of the trailing edges of the block.
		This means that when something changes, the caller must
		also update the meshes of the blocks at the leading edges.

		NOTE: This is the slowest part of this method.
	*/
	{
		// 4-23ms for MAP_BLOCKSIZE=16  (NOTE: probably outdated)
		//TimeTaker timer2("updateAllFastFaceRows()");
		updateAllFastFaceRows(data, fastfaces_new, step);
	}
	// End of slow part

	//if (data->debug) infostream<<" step="<<step<<" fastfaces_new.size="<<fastfaces_new.size()<<std::endl;

	/*
		Convert FastFaces to MeshCollector
	*/

	MeshCollector collector(m_use_tangent_vertices);

	{
		// avg 0ms (100ms spikes when loading textures the first time)
		// (NOTE: probably outdated)
		//TimeTaker timer2("MeshCollector building");

		for (u32 i = 0; i < fastfaces_new.size(); i++) {
			FastFace &f = fastfaces_new[i];

			const u16 indices[] = {0,1,2,2,3,0};
			const u16 indices_alternate[] = {0,1,3,2,3,1};

			if(f.tile.texture == NULL)
				continue;

			const u16 *indices_p = indices;

			/*
				Revert triangles for nicer looking gradient if vertices
				1 and 3 have same color or 0 and 2 have different color.
				getRed() is the day color.
			*/
			if(f.vertices[0].Color.getRed() != f.vertices[2].Color.getRed()
					|| f.vertices[1].Color.getRed() == f.vertices[3].Color.getRed())
				indices_p = indices_alternate;

			collector.append(f.tile, f.vertices, 4, indices_p, 6);
		}
	}

	/*
		Add special graphics:
		- torches
		- flowing water
		- fences
		- whatever
	*/

	if(step <= 1)
	mapblock_mesh_generate_special(data, collector);

	/*
		Convert MeshCollector to SMesh
	*/

	for(u32 i = 0; i < collector.prebuffers.size(); i++)
	{
		PreMeshBuffer &p = collector.prebuffers[i];

		if (step <= data->draw_control.farmesh || !data->draw_control.farmesh) {
		// Generate animation data
		// - Cracks
		if(p.tile.material_flags & MATERIAL_FLAG_CRACK)
		{
			// Find the texture name plus ^[crack:N:
			std::ostringstream os(std::ios::binary);
			os<<m_tsrc->getTextureName(p.tile.texture_id)<<"^[crack";
			if(p.tile.material_flags & MATERIAL_FLAG_CRACK_OVERLAY)
				os<<"o";  // use ^[cracko
			os<<":"<<(u32)p.tile.animation_frame_count<<":";
			m_crack_materials.insert(std::make_pair(i, os.str()));
			// Replace tile texture with the cracked one
			p.tile.texture = m_tsrc->getTextureForMesh(
					os.str()+"0",
					&p.tile.texture_id);
		}
		}
		// - Texture animation
		if(p.tile.material_flags & MATERIAL_FLAG_ANIMATION_VERTICAL_FRAMES && !p.tile.frames.empty())
		{
			// Add to MapBlockMesh in order to animate these tiles
			m_animation_tiles[i] = p.tile;
			m_animation_frames[i] = 0;
			if(g_settings->getBool("desynchronize_mapblock_texture_animation")){
				// Get starting position from noise
				m_animation_frame_offsets[i] = 100000 * (2.0 + noise3d(
						data->m_blockpos.X, data->m_blockpos.Y,
						data->m_blockpos.Z, 0));
			} else {
				// Play all synchronized
				m_animation_frame_offsets[i] = 0;
			}
			// Replace tile texture with the first animation frame
			FrameSpec animation_frame = p.tile.frames[0];
			p.tile.texture = animation_frame.texture;
		}

		u32 vertex_count = m_use_tangent_vertices ?
			p.tangent_vertices.size() : p.vertices.size();
		for (u32 j = 0; j < vertex_count; j++) {
			v3f *Normal;
			video::SColor *vc;
			if (m_use_tangent_vertices) {
				vc = &p.tangent_vertices[j].Color;
				Normal = &p.tangent_vertices[j].Normal;
			} else {
				vc = &p.vertices[j].Color;
				Normal = &p.vertices[j].Normal;
			}
			// Note applyFacesShading second parameter is precalculated sqrt
			// value for speed improvement
			// Skip it for lightsources and top faces.
			if (!vc->getBlue()) {
				if (Normal->Y < -0.5) {
					applyFacesShading(*vc, 0.447213);
				} else if (Normal->X > 0.5) {
					applyFacesShading(*vc, 0.670820);
				} else if (Normal->X < -0.5) {
					applyFacesShading(*vc, 0.670820);
				} else if (Normal->Z > 0.5) {
					applyFacesShading(*vc, 0.836660);
				} else if (Normal->Z < -0.5) {
					applyFacesShading(*vc, 0.836660);
				}
			}
			if (!m_enable_shaders) {
				// - Classic lighting (shaders handle this by themselves)
				// Set initial real color and store for later updates
				u8 day = vc->getRed();
				u8 night = vc->getGreen();
				finalColorBlend(*vc, day, night, 1000);
				if (day != night) {
					m_daynight_diffs[i][j] = std::make_pair(day, night);
				}
			}
		}

		// Create material
		video::SMaterial material;
		material.setFlag(video::EMF_LIGHTING, false);
		material.setFlag(video::EMF_BACK_FACE_CULLING, true);
		material.setFlag(video::EMF_BILINEAR_FILTER, false);
		material.setFlag(video::EMF_FOG_ENABLE, true);
		//material.setFlag(video::EMF_WIREFRAME, true);

		material.setTexture(0, p.tile.texture);

		if (m_enable_shaders) {
			material.MaterialType = m_shdrsrc->getShaderInfo(p.tile.shader_id).material;
			p.tile.applyMaterialOptionsWithShaders(material);
			if (p.tile.normal_texture) {
				material.setTexture(1, p.tile.normal_texture);
			}
			material.setTexture(2, p.tile.flags_texture);
		} else {
			p.tile.applyMaterialOptions(material);
		}

		scene::SMesh *mesh = (scene::SMesh *)m_mesh;

		// Create meshbuffer, add to mesh
		if (m_use_tangent_vertices) {
			scene::SMeshBufferTangents *buf = new scene::SMeshBufferTangents();
			// Set material
			buf->Material = material;
			// Add to mesh
			mesh->addMeshBuffer(buf);
			// Mesh grabbed it
			buf->drop();
			buf->append(&p.tangent_vertices[0], p.tangent_vertices.size(),
				&p.indices[0], p.indices.size());
		} else {
			scene::SMeshBuffer *buf = new scene::SMeshBuffer();
			// Set material
			buf->Material = material;
			// Add to mesh
			mesh->addMeshBuffer(buf);
			// Mesh grabbed it
			buf->drop();
			buf->append(&p.vertices[0], p.vertices.size(),
				&p.indices[0], p.indices.size());
		}
	}

	/*
		Do some stuff to the mesh
	*/
	m_camera_offset = camera_offset;

	v3f t = v3f(0,0,0);
	if (step>1) {
		translateMesh(m_mesh, v3f(HBS, 0, HBS));
		scaleMesh(m_mesh, v3f(step,step,step));
		t = v3f( -HBS, -BS*step/2+1.4142135623731*BS, -HBS); //magic number is sqrt(2)
	}
	translateMesh(m_mesh,
		intToFloat(data->m_blockpos * MAP_BLOCKSIZE - camera_offset, BS) + t);

	if (m_use_tangent_vertices) {
		scene::IMeshManipulator* meshmanip =
			m_gamedef->getSceneManager()->getMeshManipulator();
		meshmanip->recalculateTangents(m_mesh, true, false, false);
	}

	if (m_mesh)
	{
#if 0
		// Usually 1-700 faces and 1-7 materials
		infostream<<"Updated MapBlock mesh p="<<data->m_blockpos<<" has "<<fastfaces_new.size()<<" faces "
				<<"and uses "<<m_mesh->getMeshBufferCount()
				<<" materials "<<" step="<<step<<" range="<<data->range<< " mesh="<<m_mesh<<std::endl;
#endif

		// Use VBO for mesh (this just would set this for ever buffer)
		if (m_enable_vbo) {
			m_mesh->setHardwareMappingHint(scene::EHM_STATIC);
		}
	}

	//std::cout<<"added "<<fastfaces.getSize()<<" faces."<<std::endl;

	// Check if animation is required for this mesh
	m_has_animation =
		!m_crack_materials.empty() ||
		!m_daynight_diffs.empty() ||
		!m_animation_tiles.empty();
}

MapBlockMesh::~MapBlockMesh()
{
	if (!m_mesh)
		return;

	//if (m_enable_vbo && m_mesh) {
		for (u32 i = 0; i < m_mesh->getMeshBufferCount(); i++) {
			scene::IMeshBuffer *buf = m_mesh->getMeshBuffer(i);
			m_driver->removeHardwareBuffer(buf);
		}
	//}
	m_mesh->drop();
	m_mesh = NULL;
	delete m_minimap_mapblock;
	m_minimap_mapblock = nullptr;
}

bool MapBlockMesh::animate(bool faraway, float time, int crack, u32 daynight_ratio)
{
	if(!m_has_animation)
	{
		m_animation_force_timer = 100000;
		return false;
	}

#if __ANDROID__
	m_animation_force_timer = myrand_range(500, 1000);
#else
	m_animation_force_timer = myrand_range(5, 100);
#endif

	m_animation_force_timer *= step;

	// Cracks
	if (step <= 1)
	if(crack != m_last_crack)
	{
		for (UNORDERED_MAP<u32, std::string>::iterator i = m_crack_materials.begin();
				i != m_crack_materials.end(); ++i) {
			scene::IMeshBuffer *buf = m_mesh->getMeshBuffer(i->first);
			std::string basename = i->second;

			// Create new texture name from original
			std::ostringstream os;
			os<<basename<<crack;
			u32 new_texture_id = 0;
			video::ITexture *new_texture =
				m_tsrc->getTextureForMesh(os.str(), &new_texture_id);
			buf->getMaterial().setTexture(0, new_texture);

			// If the current material is also animated,
			// update animation info
			UNORDERED_MAP<u32, TileSpec>::iterator anim_iter =
					m_animation_tiles.find(i->first);
			if (anim_iter != m_animation_tiles.end()){
				TileSpec &tile = anim_iter->second;
				tile.texture = new_texture;
				tile.texture_id = new_texture_id;
				// force animation update
				m_animation_frames[i->first] = -1;
			}
		}

		m_last_crack = crack;
	}

	// Texture animation
	if (step <= 1)
	for(auto i = m_animation_tiles.begin();
			i != m_animation_tiles.end(); ++i) {
		const TileSpec &tile = i->second;
		// Figure out current frame
		int frameoffset = m_animation_frame_offsets[i->first];
		int frame = (int)(time * 1000 / tile.animation_frame_length_ms
				+ frameoffset) % (tile.animation_frame_count ? tile.animation_frame_count : 1);
		// If frame doesn't change, skip
		if(frame == m_animation_frames[i->first])
			continue;

		m_animation_frames[i->first] = frame;

		scene::IMeshBuffer *buf = m_mesh->getMeshBuffer(i->first);

		FrameSpec animation_frame = tile.frames[frame];
		buf->getMaterial().setTexture(0, animation_frame.texture);
		if (m_enable_shaders) {
			if (animation_frame.normal_texture) {
				buf->getMaterial().setTexture(1, animation_frame.normal_texture);
			}
			buf->getMaterial().setTexture(2, animation_frame.flags_texture);
		}
	}

	// Day-night transition
	if(!m_enable_shaders && (daynight_ratio != m_last_daynight_ratio))
	{
		// Force reload mesh to VBO
		if (m_enable_vbo) {
			m_mesh->setDirty();
		}
		for(std::map<u32, std::map<u32, std::pair<u8, u8> > >::iterator
				i = m_daynight_diffs.begin();
				i != m_daynight_diffs.end(); ++i)
		{
			scene::IMeshBuffer *buf = m_mesh->getMeshBuffer(i->first);
			buf->setDirty(irr::scene::EBT_VERTEX);
			video::S3DVertex *vertices = (video::S3DVertex *)buf->getVertices();
			for(std::map<u32, std::pair<u8, u8 > >::iterator
					j = i->second.begin();
					j != i->second.end(); ++j)
			{
				u8 day = j->second.first;
				u8 night = j->second.second;
				finalColorBlend(vertices[j->first].Color, day, night, daynight_ratio);
			}
		}
		m_last_daynight_ratio = daynight_ratio;
	}

	return true;
}

bool MapBlockMesh::updateCameraOffset(v3s16 camera_offset)
{
	if (camera_offset != m_camera_offset) {
		translateMesh(m_mesh, intToFloat(m_camera_offset-camera_offset, BS));
		if (m_enable_vbo) {
			m_mesh->setDirty();
		}
		m_camera_offset = camera_offset;
		return true;
	}
	return false;
}

/*
	MeshCollector
*/

void MeshCollector::append(const TileSpec &tile,
		const video::S3DVertex *vertices, u32 numVertices,
		const u16 *indices, u32 numIndices)
{
	if (numIndices > 65535) {
		dstream<<"FIXME: MeshCollector::append() called with numIndices="<<numIndices<<" (limit 65535)"<<std::endl;
		return;
	}

	PreMeshBuffer *p = NULL;
	for (u32 i = 0; i < prebuffers.size(); i++) {
		PreMeshBuffer &pp = prebuffers[i];
		if (pp.tile != tile)
			continue;
		if (pp.indices.size() + numIndices > 65535)
			continue;

		p = &pp;
		break;
	}

	if (p == NULL) {
		PreMeshBuffer pp;
		pp.tile = tile;
		prebuffers.push_back(pp);
		p = &prebuffers[prebuffers.size() - 1];
	}

	u32 vertex_count;
	if (m_use_tangent_vertices) {
		vertex_count = p->tangent_vertices.size();
		for (u32 i = 0; i < numVertices; i++) {
			video::S3DVertexTangents vert(vertices[i].Pos, vertices[i].Normal,
				vertices[i].Color, vertices[i].TCoords);
			p->tangent_vertices.push_back(vert);
		}
	} else {
		vertex_count = p->vertices.size();
		for (u32 i = 0; i < numVertices; i++) {
			video::S3DVertex vert(vertices[i].Pos, vertices[i].Normal,
				vertices[i].Color, vertices[i].TCoords);
			p->vertices.push_back(vert);
		}
	}

	for (u32 i = 0; i < numIndices; i++) {
		u32 j = indices[i] + vertex_count;
		p->indices.push_back(j);
	}
}

/*
	MeshCollector - for meshnodes and converted drawtypes.
*/

void MeshCollector::append(const TileSpec &tile,
		const video::S3DVertex *vertices, u32 numVertices,
		const u16 *indices, u32 numIndices,
		v3f pos, video::SColor c)
{
	if (numIndices > 65535) {
		dstream<<"FIXME: MeshCollector::append() called with numIndices="<<numIndices<<" (limit 65535)"<<std::endl;
		return;
	}

	PreMeshBuffer *p = NULL;
	for (u32 i = 0; i < prebuffers.size(); i++) {
		PreMeshBuffer &pp = prebuffers[i];
		if(pp.tile != tile)
			continue;
		if(pp.indices.size() + numIndices > 65535)
			continue;

		p = &pp;
		break;
	}

	if (p == NULL) {
		PreMeshBuffer pp;
		pp.tile = tile;
		prebuffers.push_back(pp);
		p = &prebuffers[prebuffers.size() - 1];
	}

	u32 vertex_count;
	if (m_use_tangent_vertices) {
		vertex_count = p->tangent_vertices.size();
		for (u32 i = 0; i < numVertices; i++) {
			video::S3DVertexTangents vert(vertices[i].Pos + pos,
				vertices[i].Normal, c, vertices[i].TCoords);
			p->tangent_vertices.push_back(vert);
		}
	} else {
		vertex_count = p->vertices.size();
		for (u32 i = 0; i < numVertices; i++) {
			video::S3DVertex vert(vertices[i].Pos + pos,
				vertices[i].Normal, c, vertices[i].TCoords);
			p->vertices.push_back(vert);
		}
	}

	for (u32 i = 0; i < numIndices; i++) {
		u32 j = indices[i] + vertex_count;
		p->indices.push_back(j);
	}
}
