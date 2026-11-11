#include "Face.h"

// The skin-wrap code (vertex decoding, facegen dynamic data, skinning) is adapted
// from OStrands' Body.cpp by LeonVanWinkle (Saint), GPL-3.0. OStrands wraps the
// whole body every frame; Lacrima reads the face and the eyes once, at a finer
// voxel, finds the tear ducts on the face, and then poses each frame only the skin
// near the fluid and around the ducts.

namespace Lacrima::Face
{
	namespace
	{
		constexpr float         kWrapVoxel = 0.25f;
		constexpr float         kForget = 10.0f;
		constexpr float         kRecheck = 2.0f;
		constexpr std::uint32_t kVertexBudget = 200000;
		constexpr std::uint32_t kDynamicStride = 16;
		constexpr float         kDynamicDrift = 3.0f;
		constexpr float         kDuctFront = 0.8f;
		constexpr float         kDuctSide = 0.8f;
		constexpr float         kCornerDuct = 0.8f;  // where on the lid line the corner tears start: 0 its middle, 1 its end
		constexpr float         kLidDepth = 0.35f;  // how much distance along the face's forward axis counts when a lid point picks its skin vertex
		constexpr float         kLidSide = 1.35f;   // eye radii from the centre to the corners of the eye, where the lid line ends
		constexpr float         kLidBelow = 0.78f;  // eye radii under the centre, at the middle of the lid: on the skin just under the lower lashes, clear of the lid margin, which curls under
		constexpr float         kLidCorner = 0.15f; // ... and at the corners, where the lid line has curved up to the eye's own height
		constexpr float         kLidRowStep = 0.4f; // eye radii between the rows under the lid: three rows reach the top of the cheek
		constexpr float         kFacingFloor = -0.2f;
		// What two eyeballs measure in head-bone space (game units): about 1 unit in
		// radius (1.4 cm), 5 units apart. The ducts are placed in eye radii, so a
		// radius far off lands them far from the eye.
		constexpr float         kEyeRadius = 1.0f;
		constexpr float         kEyeRadiusMin = 0.45f;
		constexpr float         kEyeRadiusMax = 1.8f;
		constexpr float         kEyeApart = 5.0f;
		constexpr float         kEyeApartMin = 2.5f;
		constexpr float         kEyeApartMax = 8.0f;
		constexpr float         kEyeScoreUnnamed = 0.35f;  // a geometry not called an eye must measure this close to be taken
		constexpr std::size_t   kMinFacePoints = 200;
		constexpr std::size_t   kMinEyePoints = 8;
		constexpr std::size_t   kNone = static_cast<std::size_t>(-1);
		// Skin within this of a duct (bind units) is posed every frame, so a welling
		// tear always has the lid under it, whatever the region says.
		constexpr float kDuctReach = 2.5f;
		// Slack around the watch region in a joint's own space (bind units): skin
		// this close to a tear counts as under it when an expression moves it. Tight,
		// so a blink moving the lid does not stir a trail lower on the cheek.
		constexpr float kWatchSlack = 0.5f;
		// Slack around the region once it is in a joint's own space (bind units):
		// covers the blend between joints and the margin the fluid asks for anyway.
		constexpr float kRegionSlack = 2.0f;
		// A posed point that moved this much in bind space since last frame means
		// the expression is moving the skin (0.3 mm).
		constexpr float kMorphStep = 0.02f;

		struct Joint
		{
			RE::NiPointer<RE::NiAVObject> bone;
			RE::NiTransform               skinToBone;
		};

		struct Point
		{
			RE::NiPoint3                 position;
			RE::NiPoint3                 normal;
			std::array<std::uint16_t, 4> joints{};
			std::array<float, 4>         weights{};
			std::uint8_t                 dominant{ 0 };
		};

		struct Dynamic
		{
			RE::NiPointer<RE::BSGeometry>                        geometry;
			std::vector<std::pair<std::uint32_t, std::uint32_t>> points;
		};

		struct Mesh
		{
			std::vector<RE::NiPoint3>                positions;
			std::vector<RE::NiPoint3>                normals;
			std::vector<std::array<float, 4>>        weights;
			std::vector<std::array<std::uint8_t, 4>> bones;
			bool                                     dynamic{ false };
		};

		struct EyeShape
		{
			RE::NiPoint3 center;
			float        radius{ 0.0f };
			bool         valid{ false };
		};

		struct Box
		{
			RE::NiPoint3 low;
			RE::NiPoint3 high;
		};

		struct Head
		{
			RE::NiPointer<RE::NiAVObject>                       root;
			std::vector<std::pair<const void*, std::uint32_t>> signature;
			std::vector<Joint>                                  joints;
			std::vector<Point>                                  points;
			std::vector<Dynamic>                                dynamics;
			std::vector<RE::NiPoint3>                           rest;
			std::vector<Patch>                                  patches;
			std::vector<std::uint32_t>                          candidates;
			std::vector<std::uint32_t>                          aroundDucts;
			std::vector<std::uint64_t>                          stamp;
			std::vector<Box>                                    boxes;
			std::vector<Box>                                    watchBoxes;
			std::array<EyeShape, kEyes>                         eyes{};  // in head-bone space
			float                                               forward{ 1.0f };
			RE::NiPoint3                                        faceCentre;  // the face mesh's centroid, head-bone space: what decided forward
			int                                                 forwardOverride{ 0 };  // Placement::forward and eyesSpace the head was built with
			int                                                 eyesSpace{ 0 };
			Placement                                           placement{ -1.0f, -1.0f, 0, -1.0f };
			// The face mesh's own space is not the eyes mesh's (a vanilla eyes part is
			// authored around the head bone, a face part around the skeleton root), so
			// ducts are placed in head-bone space and mapped back into the face's.
			RE::NiTransform                                     faceToBone;
			RE::NiTransform                                     boneToFace;
			std::array<std::array<std::size_t, kMaxDucts>, kEyes> duct{ { { kNone, kNone, kNone, kNone, kNone, kNone }, { kNone, kNone, kNone, kNone, kNone, kNone } } };
			Ducts                                               ducts{};
			std::array<std::array<std::array<std::size_t, kLidPoints>, kLidRows>, kEyes> lid{};
			Lids                                                lids{};
			float                                               scale{ 1.0f };
			bool                                                morphing{ false };
			std::string                                         faceName;
			std::string                                         eyesName;
			std::string                                         eyesNote;  // when the eyes were measured on another geometry than the eyes part
			bool                                                candidatesLogged{ false };  // the candidate listing goes to the log once per head, not on every 2 s rebuild of one that stays broken
			std::string                                         report;
			RE::NiPointer<RE::BSGeometry>                       eyesGeometry;
			float                                               eyeEnv{ -1.0f };    // the eyes material's own values, restored after glistening
			float                                               eyeSpec{ -1.0f };
			bool                                                eyeOwned{ false };  // eyesGeometry has its own copy of the eye material
			bool                                                glistening{ false };
			float                                               glistenUntil{ 0.0f };
			std::vector<RE::NiTransform>                        world;
			std::uint64_t                                       frame{ 0 };
			bool                                                regionPosed{ false };
			Region                                              region{};
			float                                               seen{ 0.0f };
			float                                               checked{ 0.0f };
			bool                                                ok{ false };
		};

		std::unordered_map<std::uint32_t, Head> heads;
		std::uint64_t                           frame{ 1 };
		float                                   clock{ 0.0f };

		RE::BSLightingShaderMaterialEye* EyeMaterial(const Head& a_head)
		{
			auto* shader = a_head.eyesGeometry ? a_head.eyesGeometry->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
			if (!shader || shader->GetMaterialType() != RE::BSShaderMaterial::Type::kLighting) {
				return nullptr;
			}
			auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(shader->GetBaseMaterial());
			return material && material->GetFeature() == RE::BSShaderMaterial::Feature::kEye ? static_cast<RE::BSLightingShaderMaterialEye*>(material) : nullptr;
		}

		// The game shares a material between every shader property that uses the
		// same one, so the eyes of every actor with these eyes would glisten. Before
		// the first change this actor's eyes get a copy of their own: the usual
		// copy-then-SetMaterial, after which the temporary copy is freed.
		RE::BSLightingShaderMaterialEye* OwnEyeMaterial(Head& a_head)
		{
			auto* eye = EyeMaterial(a_head);
			if (a_head.eyeOwned || !eye) {
				return eye;
			}
			auto* geometry = a_head.eyesGeometry.get();
			auto* shader = netimmerse_cast<RE::BSLightingShaderProperty*>(geometry->GetGeometryRuntimeData().shaderProperty.get());
			auto* copy = shader ? static_cast<RE::BSLightingShaderMaterialBase*>(eye->Create()) : nullptr;
			if (!copy) {
				return nullptr;
			}
			copy->CopyMembers(eye);
			shader->SetMaterial(copy, true);
			shader->SetupGeometry(geometry);
			shader->FinishSetupGeometry(geometry);
			copy->~BSLightingShaderMaterialBase();
			RE::free(copy);
			a_head.eyeOwned = true;
			return EyeMaterial(a_head);
		}

		// Puts the eyes' own reflection and highlight back.
		void Dull(Head& a_head)
		{
			if (!a_head.glistening) {
				return;
			}
			a_head.glistening = false;
			if (auto* eye = EyeMaterial(a_head)) {
				eye->envMapScale = a_head.eyeEnv;
				eye->specularColorScale = a_head.eyeSpec;
			}
		}

		float Half(std::uint16_t a_value)
		{
			const std::uint32_t sign = static_cast<std::uint32_t>(a_value & 0x8000u) << 16;
			std::uint32_t       exponent = (a_value >> 10) & 0x1Fu;
			std::uint32_t       mantissa = a_value & 0x3FFu;
			std::uint32_t       bits = sign;
			if (exponent == 0) {
				if (mantissa != 0) {
					exponent = 113;
					while (!(mantissa & 0x400u)) {
						mantissa <<= 1;
						--exponent;
					}
					bits |= (exponent << 23) | ((mantissa & 0x3FFu) << 13);
				}
			} else if (exponent == 31) {
				bits |= 0x7F800000u | (mantissa << 13);
			} else {
				bits |= ((exponent + 112) << 23) | (mantissa << 13);
			}
			return std::bit_cast<float>(bits);
		}

		std::uint32_t StrideOf(const RE::BSGraphics::VertexDesc& a_desc)
		{
			return static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(a_desc) & 0xF) * 4;
		}

		bool FullPrecision(const RE::BSGraphics::VertexDesc& a_desc)
		{
			std::uint32_t position = StrideOf(a_desc);
			for (std::uint8_t attribute = RE::BSGraphics::Vertex::VA_TEXCOORD0; attribute < RE::BSGraphics::Vertex::VA_COUNT; ++attribute) {
				const auto flag = static_cast<RE::BSGraphics::Vertex::Flags>(1u << attribute);
				if (a_desc.HasFlag(flag)) {
					position = std::min(position, a_desc.GetAttributeOffset(static_cast<RE::BSGraphics::Vertex::Attribute>(attribute)));
				}
			}
			return position >= 12;
		}

		RE::NiPoint3 PositionOf(const std::uint8_t* a_vertex, bool a_full)
		{
			if (a_full) {
				std::array<float, 3> xyz{};
				std::memcpy(xyz.data(), a_vertex, sizeof(xyz));
				return { xyz[0], xyz[1], xyz[2] };
			}
			std::array<std::uint16_t, 3> xyz{};
			std::memcpy(xyz.data(), a_vertex, sizeof(xyz));
			return { Half(xyz[0]), Half(xyz[1]), Half(xyz[2]) };
		}

		RE::NiPoint3 Unit(RE::NiPoint3 a_vector, const RE::NiPoint3& a_fallback)
		{
			return a_vector.Unitize() > 1e-5f ? a_vector : a_fallback;
		}

		std::uint32_t VerticesOf(RE::BSGeometry* a_geometry)
		{
			auto* skin = a_geometry ? a_geometry->GetGeometryRuntimeData().skinInstance.get() : nullptr;
			auto* partition = skin ? skin->skinPartition.get() : nullptr;
			return partition ? partition->vertexCount : 0u;
		}

		bool Hidden(RE::NiAVObject* a_object, RE::NiAVObject* a_root)
		{
			for (auto* node = a_object; node && node != a_root; node = node->parent) {
				if (node->GetAppCulled()) {
					return true;
				}
			}
			return false;
		}

		// A morphing shape keeps its posed positions in a dynamic buffer (float4 per
		// vertex); when it holds one, those replace the stored positions.
		void ReadDynamic(RE::BSGeometry* a_geometry, std::uint32_t a_count, bool a_stored, Mesh& a_out)
		{
			auto* dynamic = a_geometry->AsDynamicTriShape();
			if (!dynamic) {
				return;
			}
			std::vector<RE::NiPoint3> moved(a_count);
			bool                      read = false;
			{
				auto&               runtime = dynamic->GetDynamicTrishapeRuntimeData();
				RE::BSSpinLockGuard guard(runtime.lock);
				if (runtime.dynamicData && runtime.dataSize >= a_count * kDynamicStride) {
					const auto* source = static_cast<const std::uint8_t*>(runtime.dynamicData);
					for (std::uint32_t v = 0; v < a_count; ++v) {
						std::array<float, 3> xyz{};
						std::memcpy(xyz.data(), source + static_cast<std::size_t>(v) * kDynamicStride, sizeof(xyz));
						moved[v] = { xyz[0], xyz[1], xyz[2] };
					}
					read = true;
				}
			}
			float drift = 0.0f;
			for (std::uint32_t v = 0; read && a_stored && v < a_count; ++v) {
				drift += moved[v].GetDistance(a_out.positions[v]);
			}
			if (read && (!a_stored || drift < kDynamicDrift * static_cast<float>(a_count))) {
				a_out.positions = std::move(moved);
				a_out.dynamic = true;
			}
		}

		// A shape with no skin, hung under a bone as it is: its positions come from
		// the renderer's copy of its vertex buffer (or its dynamic data), in its own
		// space; the caller takes them into head-bone space through the node
		// transforms. Only used to measure eyes: it reads no normals or weights.
		bool ReadLoose(RE::BSGeometry* a_geometry, Mesh& a_out)
		{
			auto* tri = a_geometry ? a_geometry->AsTriShape() : nullptr;
			auto* renderer = a_geometry ? a_geometry->GetGeometryRuntimeData().rendererData : nullptr;
			const std::uint32_t count = tri ? tri->GetTrishapeRuntimeData().vertexCount : 0u;
			if (!renderer || count == 0 || count > kVertexBudget) {
				return false;
			}
			const auto&         desc = renderer->vertexDesc;
			const std::uint32_t stride = StrideOf(desc);
			const bool          stored = desc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX) && renderer->rawVertexData && stride >= 8 && stride <= 128;
			const bool          full = FullPrecision(desc);
			a_out = {};
			a_out.positions.assign(count, RE::NiPoint3{});
			for (std::uint32_t v = 0; stored && v < count; ++v) {
				a_out.positions[v] = PositionOf(renderer->rawVertexData + static_cast<std::size_t>(v) * stride, full);
			}
			ReadDynamic(a_geometry, count, stored, a_out);
			return stored || a_out.dynamic;
		}

		std::uint32_t LooseVerticesOf(RE::BSGeometry* a_geometry)
		{
			auto* tri = a_geometry ? a_geometry->AsTriShape() : nullptr;
			return tri ? tri->GetTrishapeRuntimeData().vertexCount : 0u;
		}

		bool ReadMesh(RE::BSGeometry* a_geometry, Mesh& a_out)
		{
			auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
			auto* partition = skin ? skin->skinPartition.get() : nullptr;
			if (!skin || !skin->skinData || !skin->bones || !partition || !partition->numPartitions ||
				partition->vertexCount > kVertexBudget) {
				return false;
			}
			const auto& first = partition->partitions[0];
			if (!first.buffData || !first.buffData->rawVertexData) {
				return false;
			}
			const std::uint32_t stride = StrideOf(first.vertexDesc);
			const std::uint32_t skinning = first.vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_SKINNING);
			if (stride < 8 || stride > 128 || !first.vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_SKINNED) || skinning + 12 > stride) {
				return false;
			}
			const std::uint32_t count = partition->vertexCount;
			const bool          stored = first.vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX);
			const bool          full = FullPrecision(first.vertexDesc);
			const std::uint8_t* data = first.buffData->rawVertexData;
			a_out.positions.resize(count);
			a_out.weights.resize(count);
			a_out.bones.resize(count);
			a_out.dynamic = false;
			for (std::uint32_t v = 0; v < count; ++v) {
				const std::uint8_t* vertex = data + static_cast<std::size_t>(v) * stride;
				a_out.positions[v] = stored ? PositionOf(vertex, full) : RE::NiPoint3{};
				std::array<std::uint16_t, 4> halves{};
				std::memcpy(halves.data(), vertex + skinning, sizeof(halves));
				std::memcpy(a_out.bones[v].data(), vertex + skinning + 8, sizeof(a_out.bones[v]));
				for (std::size_t j = 0; j < halves.size(); ++j) {
					a_out.weights[v][j] = Half(halves[j]);
				}
			}
			ReadDynamic(a_geometry, count, stored, a_out);
			if (!stored && !a_out.dynamic) {
				return false;
			}
			a_out.normals.assign(count, RE::NiPoint3{});
			for (std::uint32_t p = 0; p < partition->numPartitions; ++p) {
				const auto& part = partition->partitions[p];
				if (!part.triList) {
					continue;
				}
				std::uint32_t highest = 0;
				for (std::uint32_t i = 0; i < part.triangles * 3u; ++i) {
					highest = std::max<std::uint32_t>(highest, part.triList[i]);
				}
				const bool local = part.vertexMap && highest < part.vertices && part.vertices < count;
				const auto index = [&](std::uint32_t a_corner) -> std::uint32_t {
					const std::uint32_t value = part.triList[a_corner];
					return local ? part.vertexMap[value] : value;
				};
				for (std::uint32_t t = 0; t < part.triangles; ++t) {
					const std::uint32_t a = index(3 * t);
					const std::uint32_t b = index(3 * t + 1);
					const std::uint32_t c = index(3 * t + 2);
					if (a >= count || b >= count || c >= count) {
						continue;
					}
					const RE::NiPoint3 normal = (a_out.positions[b] - a_out.positions[a]).Cross(a_out.positions[c] - a_out.positions[a]);
					a_out.normals[a] += normal;
					a_out.normals[b] += normal;
					a_out.normals[c] += normal;
				}
			}
			return true;
		}

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		RE::BSGeometry* HeadPart(RE::Actor* a_actor, RE::NiAVObject* a_root, RE::BGSHeadPart::HeadPartType a_type)
		{
			auto* npc = a_actor->GetActorBase();
			auto* part = npc ? npc->GetCurrentHeadPartByType(a_type) : nullptr;
			const char* name = part ? part->formEditorID.c_str() : nullptr;
			if (!name || !*name) {
				return nullptr;
			}
			auto* object = a_root->GetObjectByName(part->formEditorID);
			auto* geometry = object ? object->AsGeometry() : nullptr;
			return geometry && VerticesOf(geometry) > 0 ? geometry : nullptr;
		}

		// The head bone node: the one the face is skinned to, else by name under the root.
		RE::NiAVObject* HeadBoneOf(RE::BSGeometry* a_face, RE::NiAVObject* a_root)
		{
			auto* skin = a_face ? a_face->GetGeometryRuntimeData().skinInstance.get() : nullptr;
			if (skin && skin->skinData && skin->bones) {
				const std::uint32_t bones = skin->skinData->GetBoneCount();
				for (std::uint32_t i = 0; i < bones; ++i) {
					auto* bone = skin->bones[i];
					if (bone && bone->name.c_str() && std::string_view(bone->name.c_str()) == kHeadBone) {
						return bone;
					}
				}
			}
			return a_root ? a_root->GetObjectByName(kHeadBone) : nullptr;
		}

		// The transform from a head part's mesh space into the head bone's space: the
		// skin-to-bone transform its skin carries for that bone (bone 0 when the head
		// bone is not among its bones). Identity when the mesh has no skin data.
		RE::NiTransform ToHeadBone(RE::BSGeometry* a_geometry)
		{
			auto* skin = a_geometry ? a_geometry->GetGeometryRuntimeData().skinInstance.get() : nullptr;
			if (!skin || !skin->skinData || !skin->bones || skin->skinData->GetBoneCount() == 0) {
				return RE::NiTransform{};
			}
			const std::uint32_t bones = skin->skinData->GetBoneCount();
			std::uint32_t       which = 0;
			for (std::uint32_t i = 0; i < bones; ++i) {
				const auto* bone = skin->bones[i];
				if (bone && bone->name.c_str() && std::string_view(bone->name.c_str()) == kHeadBone) {
					which = i;
					break;
				}
			}
			return skin->skinData->GetBoneDataSkinToBone(which);
		}

		// Head parts are named after their records on every head the game builds;
		// this name guess is only for a head that somehow is not.
		RE::BSGeometry* Guess(RE::NiAVObject* a_root, bool a_eyes)
		{
			RE::BSGeometry* best = nullptr;
			std::uint32_t   most = 0;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				const std::uint32_t vertices = VerticesOf(a_geometry);
				const char*         raw = a_geometry->name.c_str();
				if (vertices <= most || !raw || Hidden(a_geometry, a_root)) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				const std::string name = Lower(raw);
				const bool        eyes = name.find("eye") != std::string::npos && name.find("brow") == std::string::npos &&
				                  name.find("lash") == std::string::npos && name.find("liner") == std::string::npos &&
				                  name.find("shadow") == std::string::npos;
				const bool head = name.find("head") != std::string::npos && name.find("hair") == std::string::npos && !eyes;
				if (a_eyes ? eyes : head) {
					best = a_geometry;
					most = vertices;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return best;
		}

		// One geometry measured in head-bone space: the mesh split by side of the
		// midline, each side's centre and mean spread. score says how far it is from
		// two eyeballs (0 = exactly), plausible whether it is within reason; half
		// marks a geometry holding ONE eye (an eye system with a left and a right
		// mesh), which the caller pairs with its other half. The eyes part named on
		// the record is not always the eyeballs: CVEO's part is a 176-vertex mesh
		// spread 3.6 units around a point behind the eyes (an eye socket or shadow
		// shell; LL report 2026-11-09), which put the ducts on the jaw by the ears,
		// and its eyeballs are elsewhere on the head.
		struct EyesRead
		{
			std::array<EyeShape, 2> sides{};
			Mesh                    mesh;
			float                   score{ std::numeric_limits<float>::max() };
			bool                    plausible{ false };
			bool                    half{ false };   // one eye only; sides[0] is a left eye, sides[1] a right one
			bool                    loose{ false };  // read as an unskinned shape
		};

		bool MeasureEyes(RE::BSGeometry* a_eyes, const std::function<RE::NiTransform(RE::BSGeometry*, bool)>& a_toBone, EyesRead& a_out)
		{
			a_out = {};
			if (!a_eyes) {
				return false;
			}
			if (!ReadMesh(a_eyes, a_out.mesh)) {
				if (!ReadLoose(a_eyes, a_out.mesh)) {
					return false;
				}
				a_out.loose = true;
			}
			const RE::NiTransform     toBone = a_toBone(a_eyes, a_out.loose);
			std::vector<RE::NiPoint3> points;
			points.reserve(a_out.mesh.positions.size());
			std::array<RE::NiPoint3, 2> sum{};
			std::array<std::size_t, 2>  count{};
			for (const auto& position : a_out.mesh.positions) {
				const RE::NiPoint3 p = toBone * position;
				points.push_back(p);
				const std::size_t side = p.x >= 0.0f ? 1 : 0;
				sum[side] += p;
				++count[side];
			}
			auto& sides = a_out.sides;
			for (std::size_t s = 0; s < 2; ++s) {
				if (count[s] < kMinEyePoints) {
					continue;
				}
				sides[s].center = sum[s] / static_cast<float>(count[s]);
				float spread = 0.0f;
				for (const auto& p : points) {
					if ((p.x >= 0.0f ? 1u : 0u) == s) {
						spread += p.GetDistance(sides[s].center);
					}
				}
				sides[s].radius = spread / static_cast<float>(count[s]);
				sides[s].valid = sides[s].radius > 0.05f;
			}
			if (!sides[0].valid && !sides[1].valid) {
				return false;
			}
			float score = 0.0f;
			bool  plausible = true;
			for (const auto& side : sides) {
				if (!side.valid) {
					continue;
				}
				score += std::abs(std::log(side.radius / kEyeRadius));
				plausible = plausible && side.radius >= kEyeRadiusMin && side.radius <= kEyeRadiusMax;
			}
			if (sides[0].valid && sides[1].valid) {
				const float apart = sides[0].center.GetDistance(sides[1].center);
				score += std::abs(std::log(std::max(apart, 1e-3f) / kEyeApart));
				plausible = plausible && apart >= kEyeApartMin && apart <= kEyeApartMax;
			} else {
				// one eye: a left mesh lies on -x, a right one on +x; a single eye sitting
				// astride the midline is not an eye
				const auto& eye = sides[0].valid ? sides[0] : sides[1];
				a_out.half = true;
				plausible = plausible && std::abs(eye.center.x) > eye.radius;
			}
			a_out.score = score;
			a_out.plausible = plausible;
			return true;
		}

		// Adds a mesh's vertices as wrap points: deduplicated on a fine voxel, with
		// normals turned outward. Returns how many points it added.
		std::size_t Add(Head& a_head, RE::BSGeometry* a_geometry, const Mesh& a_mesh,
			const std::function<bool(const RE::NiPoint3&, const RE::NiPoint3&)>& a_outward)
		{
			auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
			if (!skin || !skin->skinData || !skin->bones) {
				return 0;
			}
			const std::uint32_t bones = skin->skinData->GetBoneCount();
			std::size_t         facing = 0;
			std::size_t         outward = 0;
			for (std::size_t v = 0; v < a_mesh.positions.size(); ++v) {
				if (a_mesh.normals[v].SqrLength() < 1e-12f) {
					continue;
				}
				++facing;
				outward += a_outward(a_mesh.positions[v], a_mesh.normals[v]);
			}
			const float flip = facing && outward * 2 < facing ? -1.0f : 1.0f;

			const std::size_t base = a_head.joints.size();
			for (std::uint32_t i = 0; i < bones; ++i) {
				a_head.joints.push_back({ RE::NiPointer<RE::NiAVObject>(skin->bones[i]), skin->skinData->GetBoneDataSkinToBone(i) });
			}
			const auto cell = [](float a_value) {
				return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::floor(a_value / kWrapVoxel)) + (1 << 19)) & 0xFFFFF;
			};
			std::unordered_set<std::uint64_t> taken;
			Dynamic                           dynamic;
			std::size_t                       kept = 0;
			for (std::uint32_t v = 0; v < a_mesh.positions.size(); ++v) {
				RE::NiPoint3 normal = a_mesh.normals[v] * flip;
				if (normal.Unitize() < 1e-6f) {
					continue;
				}
				const auto& position = a_mesh.positions[v];
				if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
					continue;
				}
				const std::uint64_t key = (cell(position.x) << 43) | (cell(position.y) << 23) | (cell(position.z) << 3) |
				                          (normal.x > 0.0f ? 4u : 0u) | (normal.y > 0.0f ? 2u : 0u) | (normal.z > 0.0f ? 1u : 0u);
				if (!taken.insert(key).second) {
					continue;
				}
				Point point{ position, normal, {}, {} };
				float total = 0.0f;
				float heaviest = 0.0f;
				for (std::size_t j = 0; j < 4; ++j) {
					const std::uint8_t bone = a_mesh.bones[v][j];
					const float        weight = a_mesh.weights[v][j];
					if (bone < bones && skin->bones[bone] && std::isfinite(weight) && weight > 0.0f) {
						point.joints[j] = static_cast<std::uint16_t>(base + bone);
						point.weights[j] = weight;
						total += weight;
						if (weight > heaviest) {
							heaviest = weight;
							point.dominant = static_cast<std::uint8_t>(j);
						}
					}
				}
				if (!(total > 0.0f)) {
					continue;
				}
				for (auto& weight : point.weights) {
					weight /= total;
				}
				if (a_mesh.dynamic) {
					dynamic.points.emplace_back(static_cast<std::uint32_t>(a_head.points.size()), v);
				}
				a_head.points.push_back(point);
				++kept;
			}
			if (!dynamic.points.empty()) {
				dynamic.geometry.reset(a_geometry);
				a_head.dynamics.push_back(std::move(dynamic));
			}
			return kept;
		}

		void PlaceDucts(Head& a_head, const Placement& a_placement)
		{
			a_head.placement = a_placement;
			const int    streams = std::clamp(a_placement.streams, 1, static_cast<int>(kMaxStreams));
			const auto   facing = a_head.boneToFace.rotate * RE::NiPoint3{ 0.0f, a_head.forward, 0.0f };
			for (std::size_t e = 0; e < kEyes; ++e) {
				a_head.duct[e].fill(kNone);
				const auto& eye = a_head.eyes[e];
				if (!eye.valid) {
					continue;
				}
				const float out = eye.center.x >= 0.0f ? 1.0f : -1.0f;
				const float r = std::clamp(eye.radius, kEyeRadiusMin, kEyeRadiusMax);  // a wild measurement must not throw the ducts off the face
				for (int s = 0; s < streams; ++s) {
					// streams sit evenly across a_spread of the lid, centred on a_position
					const float along = streams == 1 ? a_placement.position :
					                                   std::clamp(a_placement.position + a_placement.spread * (static_cast<float>(s) / static_cast<float>(streams - 1) - 0.5f), 0.0f, 1.0f);
					const RE::NiPoint3 inBone = eye.center + RE::NiPoint3{ out * r * kDuctSide * (2.0f * along - 1.0f), a_head.forward * r * kDuctFront, -r * a_placement.below };
					const RE::NiPoint3 target = a_head.boneToFace * inBone;
					float              best = std::numeric_limits<float>::max();
					for (std::size_t k = 0; k < a_head.points.size(); ++k) {
						const auto& point = a_head.points[k];
						if (point.normal.Dot(facing) < kFacingFloor) {
							continue;
						}
						const float distance = point.position.GetSquaredDistance(target);
						if (distance < best) {
							best = distance;
							a_head.duct[e][static_cast<std::size_t>(s)] = k;
						}
					}
				}
			}
			// The corners of the eye, for the tears that start there: on the lid line
			// the mascara follows, a little in from its ends. Picked across the face
			// like the lid points (depth counts for little), or the outer one lands
			// on the more forward skin near the middle of the lid.
			for (std::size_t e = 0; e < kEyes; ++e) {
				const auto& eye = a_head.eyes[e];
				if (!eye.valid) {
					continue;
				}
				const float out = eye.center.x >= 0.0f ? 1.0f : -1.0f;
				const float r = std::clamp(eye.radius, kEyeRadiusMin, kEyeRadiusMax);
				for (const std::size_t corner : { kInnerCorner, kOuterCorner }) {
					const float        across = corner == kInnerCorner ? -kCornerDuct : kCornerDuct;
					const float        curve = std::sqrt(std::max(0.0f, 1.0f - across * across));
					const float        below = kLidCorner + (a_placement.below - kLidCorner) * curve;
					const RE::NiPoint3 inBone = eye.center + RE::NiPoint3{ out * r * kLidSide * across, a_head.forward * r * kDuctFront, -r * below };
					const RE::NiPoint3 target = a_head.boneToFace * inBone;
					float              best = std::numeric_limits<float>::max();
					for (std::size_t k = 0; k < a_head.points.size(); ++k) {
						const auto& point = a_head.points[k];
						if (point.normal.Dot(facing) < kFacingFloor) {
							continue;
						}
						const RE::NiPoint3 delta = point.position - target;
						const float        depth = delta.Dot(facing);
						const float        distance = delta.SqrLength() - depth * depth + kLidDepth * kLidDepth * depth * depth;
						if (distance < best) {
							best = distance;
							a_head.duct[e][corner] = k;
						}
					}
				}
			}
			for (std::size_t e = 0; e < kEyes; ++e) {
				const auto& eye = a_head.eyes[e];
				if (a_head.duct[e][0] != kNone) {
					const auto&        d = a_head.points[a_head.duct[e][0]];
					const RE::NiPoint3 inBone = a_head.faceToBone * d.position;
					logger::info("{} eye in head-bone space: centre ({:.2f}, {:.2f}, {:.2f}), radius {:.2f}; first duct ({:.2f}, {:.2f}, {:.2f}), {:.2f} below the eye centre, {} stream(s); face forward {:+.0f}y",
						e == kLeft ? "left" : "right", eye.center.x, eye.center.y, eye.center.z, eye.radius, inBone.x, inBone.y, inBone.z,
						eye.center.z - inBone.z, streams, a_head.forward);
				}
			}
			// The skin under the lower lid, inner corner to outer, in rows downward, for
			// the mascara smudge. The lid line follows the eye: lowest under the middle,
			// curving up into both corners, which is where the mascara pools.
			for (std::size_t e = 0; e < kEyes; ++e) {
				for (auto& row : a_head.lid[e]) {
					row.fill(kNone);
				}
				const auto& eye = a_head.eyes[e];
				if (!eye.valid) {
					continue;
				}
				const float out = eye.center.x >= 0.0f ? 1.0f : -1.0f;
				const float r = std::clamp(eye.radius, kEyeRadiusMin, kEyeRadiusMax);
				for (std::size_t row = 0; row < kLidRows; ++row) {
					for (std::size_t i = 0; i < kLidPoints; ++i) {
						const float        across = 2.0f * static_cast<float>(i) / static_cast<float>(kLidPoints - 1) - 1.0f;  // -1 inner corner .. 1 outer
						const float        curve = std::sqrt(std::max(0.0f, 1.0f - across * across));
						const float        below = kLidCorner + (kLidBelow - kLidCorner) * curve + kLidRowStep * static_cast<float>(row);
						const RE::NiPoint3 inBone = eye.center + RE::NiPoint3{ out * r * kLidSide * across, a_head.forward * r * kDuctFront, -r * below };
						const RE::NiPoint3 target = a_head.boneToFace * inBone;
						float              best = std::numeric_limits<float>::max();
						for (std::size_t k = 0; k < a_head.points.size(); ++k) {
							const auto& point = a_head.points[k];
							if (point.normal.Dot(facing) < kFacingFloor) {
								continue;
							}
							// Depth counts for little: the target floats in front of the eye,
							// and toward the temple the skin falls well back from it, so by
							// plain distance the outer points all landed on the more forward
							// skin near the middle of the lid and the smudge stopped short of
							// the corner.
							const RE::NiPoint3 delta = point.position - target;
							const float        depth = delta.Dot(facing);
							const float        distance = delta.SqrLength() - depth * depth + kLidDepth * kLidDepth * depth * depth;
							if (distance < best) {
								best = distance;
								a_head.lid[e][row][i] = k;
							}
						}
					}
				}
			}
			// The skin around the ducts and the lids is posed every frame, whatever the
			// fluid asks.
			a_head.aroundDucts.clear();
			const auto around = [&](std::size_t a_index) {
				if (a_index == kNone) {
					return;
				}
				const RE::NiPoint3 center = a_head.points[a_index].position;
				for (std::size_t k = 0; k < a_head.points.size(); ++k) {
					if (a_head.points[k].position.GetSquaredDistance(center) < kDuctReach * kDuctReach) {
						a_head.aroundDucts.push_back(static_cast<std::uint32_t>(k));
					}
				}
			};
			for (const auto& perEye : a_head.duct) {
				for (const auto d : perEye) {
					around(d);
				}
			}
			for (const auto& perEye : a_head.lid) {
				for (const auto& row : perEye) {
					for (const auto l : row) {
						around(l);
					}
				}
			}
			std::ranges::sort(a_head.aroundDucts);
			a_head.aroundDucts.erase(std::ranges::unique(a_head.aroundDucts).begin(), a_head.aroundDucts.end());
		}

		void Build(RE::Actor* a_actor, Head& a_head)
		{
			Dull(a_head);
			a_head.eyesGeometry.reset();
			a_head.eyeOwned = false;
			a_head.joints.clear();
			a_head.points.clear();
			a_head.dynamics.clear();
			a_head.rest.clear();
			a_head.patches.clear();
			a_head.candidates.clear();
			a_head.aroundDucts.clear();
			a_head.stamp.clear();
			a_head.eyes = {};
			for (auto& perEye : a_head.duct) {
				perEye.fill(kNone);
			}
			for (auto& perEye : a_head.lid) {
				for (auto& row : perEye) {
					row.fill(kNone);
				}
			}
			a_head.lids = {};
			a_head.ok = false;
			a_head.faceName.clear();
			a_head.eyesName.clear();
			a_head.report.clear();
			auto* root = a_head.root.get();

			auto* face = HeadPart(a_actor, root, RE::BGSHeadPart::HeadPartType::kFace);
			auto* eyes = HeadPart(a_actor, root, RE::BGSHeadPart::HeadPartType::kEyes);
			const bool guessedFace = !face;
			const bool guessedEyes = !eyes;
			face = face ? face : Guess(root, false);
			eyes = eyes ? eyes : Guess(root, true);
			if (!face || !eyes) {
				a_head.report = !face ? "no face mesh found" : "no eyes mesh found";
				return;
			}
			a_head.faceName = std::format("{}{}", face->name.c_str() ? face->name.c_str() : "?", guessedFace ? " (guessed by name)" : "");
			a_head.eyesName = std::format("{}{}", eyes->name.c_str() ? eyes->name.c_str() : "?", guessedEyes ? " (guessed by name)" : "");

			Mesh faceMesh;
			if (!ReadMesh(face, faceMesh)) {
				a_head.report = "the face mesh has no readable vertices";
				return;
			}

			// Each head part is authored in its own space (a vanilla eyes part around
			// the head bone, a face part around the skeleton root); the skin-to-bone
			// transforms of the head bone take both into head-bone space, where the
			// eyes are measured and the ducts aimed.
			a_head.faceToBone = ToHeadBone(face);
			a_head.boneToFace = a_head.faceToBone.Invert();
			RE::NiAVObject* headBone = HeadBoneOf(face, root);
			const auto      toBone = [&](RE::BSGeometry* a_geometry, bool a_loose) -> RE::NiTransform {
				if (a_loose) {
					// an unskinned shape hangs under a bone as it is: node transforms take
					// it to the world and the head bone's take it back into bone space
					return headBone ? headBone->world.Invert() * a_geometry->world : RE::NiTransform{};
				}
				if (a_head.eyesSpace == 1) {
					return a_head.faceToBone;  // [debug] eyes_space: the eyes drawn in the face mesh's space
				}
				if (a_head.eyesSpace == 2) {
					return RE::NiTransform{};  // ... or already in head-bone space
				}
				return ToHeadBone(a_geometry);
			};

			// Eyes: one mesh holds both, split by side of the midline. They only place
			// the ducts; the eyeball is not a surface tears run on. The eyes part named
			// on the record first; when it does not measure as two eyeballs, the part's
			// extra parts and every other geometry on the actor are measured, skinned or
			// not, and the most eyeball-like taken: one mesh with both eyes, or a left
			// and a right mesh paired (one not called an eye only if it measures very
			// close). So an eyes system whose named part is a shell still places the
			// ducts on the lids. Every candidate measured is logged, for the next head
			// that fails.
			EyesRead        read;
			RE::BSGeometry* chosen = nullptr;
			const bool      named = MeasureEyes(eyes, toBone, read);
			a_head.eyesNote.clear();
			if (named && read.plausible && !read.half) {
				chosen = eyes;
			} else {
				const auto describe = [](const EyesRead& a_read) {
					if (a_read.half) {
						const auto& eye = a_read.sides[0].valid ? a_read.sides[0] : a_read.sides[1];
						return std::format("one eye, radius {:.2f} at x {:+.1f}", eye.radius, eye.center.x);
					}
					return std::format("radius {:.2f} / {:.2f}, {:.1f} apart", a_read.sides[0].radius, a_read.sides[1].radius,
						a_read.sides[0].valid && a_read.sides[1].valid ? a_read.sides[0].center.GetDistance(a_read.sides[1].center) : 0.0f);
				};
				const auto nameOf = [](RE::BSGeometry* a_geometry) { return a_geometry && a_geometry->name.c_str() ? a_geometry->name.c_str() : "?"; };
				const auto eyeNamed = [](RE::BSGeometry* a_geometry) {
					const char* raw = a_geometry->name.c_str();
					const std::string name = raw ? Lower(raw) : std::string{};
					return name.find("eye") != std::string::npos && name.find("brow") == std::string::npos && name.find("lash") == std::string::npos &&
					       name.find("liner") == std::string::npos && name.find("shadow") == std::string::npos;
				};
				if (!a_head.candidatesLogged) {
					logger::info("{}: the eyes part {} does not look like two eyes ({}); measuring the other geometry on the head", a_actor->GetDisplayFullName(), nameOf(eyes),
						named ? describe(read) : "unreadable");
				}
				std::vector<RE::BSGeometry*> candidates;
				if (auto* npc = a_actor->GetActorBase()) {
					if (auto* part = npc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kEyes)) {
						for (auto* extra : part->extraParts) {
							auto* object = extra && extra->formEditorID.c_str() ? root->GetObjectByName(extra->formEditorID) : nullptr;
							auto* geometry = object ? object->AsGeometry() : nullptr;
							if (geometry && geometry != eyes) {
								candidates.push_back(geometry);
							}
						}
					}
				}
				RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
					if (a_geometry != eyes && a_geometry != face && (VerticesOf(a_geometry) > 0 || LooseVerticesOf(a_geometry) > 0) &&
						!Hidden(a_geometry, root) && std::ranges::find(candidates, a_geometry) == candidates.end()) {
						candidates.push_back(a_geometry);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				EyesRead                                            best;
				std::vector<std::pair<RE::BSGeometry*, EyesRead>>   halves;
				std::size_t                                         logged = a_head.candidatesLogged ? 60 : 0;
				for (auto* candidate : candidates) {
					EyesRead   measured;
					const bool readable = MeasureEyes(candidate, toBone, measured);
					if (logged < 60) {
						++logged;
						logger::info("  {}: {} vertices{}{}{}", nameOf(candidate), measured.mesh.positions.size(), measured.loose ? " (unskinned)" : "",
							readable ? ", " + describe(measured) : ", unreadable", readable && measured.plausible ? ", plausible" : "");
					}
					if (!readable || !measured.plausible) {
						continue;
					}
					const bool trusted = eyeNamed(candidate) || measured.score < kEyeScoreUnnamed;
					if (!trusted) {
						continue;
					}
					if (measured.half) {
						halves.emplace_back(candidate, std::move(measured));
					} else if (measured.score < best.score) {
						best = std::move(measured);
						chosen = candidate;
					}
				}
				a_head.candidatesLogged = true;
				// a left and a right mesh: the pair that looks most like two eyes
				RE::BSGeometry* other = nullptr;
				for (const auto& [lg, left] : halves) {
					if (!left.sides[0].valid) {
						continue;
					}
					for (const auto& [rg, right] : halves) {
						if (!right.sides[1].valid) {
							continue;
						}
						const float apart = left.sides[0].center.GetDistance(right.sides[1].center);
						const float alike = std::abs(std::log(left.sides[0].radius / right.sides[1].radius));
						if (apart < kEyeApartMin || apart > kEyeApartMax || alike > 0.5f) {
							continue;
						}
						const float score = left.score + right.score + alike + std::abs(std::log(apart / kEyeApart));
						if (score < best.score) {
							best = {};
							best.sides = { left.sides[0], right.sides[1] };
							best.mesh = right.mesh;
							best.score = score;
							best.plausible = true;
							best.loose = left.loose || right.loose;
							chosen = rg;
							other = lg;
						}
					}
				}
				if (chosen) {
					a_head.eyesNote = other ? std::format(" (measured on {} + {}: the eyes part {} does not look like two eyes, {})", nameOf(other), nameOf(chosen), nameOf(eyes),
												  named ? describe(read) : "unreadable") :
					                          std::format(" (measured on {}: the eyes part {} does not look like two eyes, {})", nameOf(chosen), nameOf(eyes),
												  named ? describe(read) : "unreadable");
					read = std::move(best);
				} else if (named) {
					chosen = eyes;
					a_head.eyesNote = std::format(" (the eyes part does not look like two eyes, {}; nothing better on the head)", describe(read));
				} else {
					a_head.report = "the eyes mesh has no readable vertices";
					return;
				}
				// two separate eye meshes: the wet-eye glisten, which works on one eye
				// material, stays off rather than wetting one eye
				if (other) {
					chosen = nullptr;
				}
			}
			a_head.eyesGeometry.reset(chosen);
			const auto& sides = read.sides;
			const auto& eyeMesh = read.mesh;

			// The face looks along +Y or -Y in head-bone space: the eyes sit in front
			// of the middle of the head. Right = forward x up, so with forward +Y the
			// actor's right eye is on +X.
			RE::NiPoint3 centroid;
			for (const auto& position : faceMesh.positions) {
				centroid += position;
			}
			centroid = centroid / static_cast<float>(std::max<std::size_t>(faceMesh.positions.size(), 1));
			const RE::NiPoint3 centroidInBone = a_head.faceToBone * centroid;
			RE::NiPoint3       eyesMiddle;
			int                valid = 0;
			for (const auto& side : sides) {
				if (side.valid) {
					eyesMiddle += side.center;
					++valid;
				}
			}
			eyesMiddle = eyesMiddle / static_cast<float>(valid);
			a_head.forward = a_head.forwardOverride != 0 ? static_cast<float>(a_head.forwardOverride) : (eyesMiddle.y >= centroidInBone.y ? 1.0f : -1.0f);
			a_head.faceCentre = centroidInBone;
			a_head.eyes[kRight] = a_head.forward > 0.0f ? sides[1] : sides[0];
			a_head.eyes[kLeft] = a_head.forward > 0.0f ? sides[0] : sides[1];

			const std::size_t facePoints = Add(a_head, face, faceMesh,
				[&](const RE::NiPoint3& a_position, const RE::NiPoint3& a_normal) { return a_normal.Dot(a_position - centroid) > 0.0f; });
			if (facePoints < kMinFacePoints) {
				a_head.report = std::format("only {} usable face points", facePoints);
				return;
			}
			a_head.report = std::format("face {} points{}, eyes {} vertices{}{}{}{}", facePoints, faceMesh.dynamic ? " (animated)" : "",
				eyeMesh.positions.size(), eyeMesh.dynamic ? " (animated)" : "", a_head.eyesNote, a_head.forwardOverride != 0 ? ", forward forced" : "",
				a_head.eyesSpace == 1 ? ", eyes taken in the face's space" : a_head.eyesSpace == 2 ? ", eyes taken in head space" : "");
			a_head.ok = true;
			a_head.placement = Placement{ -1.0f, -1.0f, 0, -1.0f };
		}

		// Poses one wrap point with the joints as they stand this frame.
		void SkinPoint(const Head& a_head, const Point& a_point, RE::NiPoint3& a_position, RE::NiPoint3& a_normal, float& a_scale)
		{
			RE::NiPoint3 position;
			RE::NiPoint3 normal;
			float        scale = 0.0f;
			for (std::size_t j = 0; j < 4; ++j) {
				const float weight = a_point.weights[j];
				if (weight <= 0.0f) {
					continue;
				}
				const auto& transform = a_head.world[a_point.joints[j]];
				position += (transform * a_point.position) * weight;
				normal += (transform.rotate * a_point.normal) * weight;
				scale += transform.scale * weight;
			}
			a_position = position;
			a_normal = Unit(normal, a_point.normal);
			a_scale = scale;
		}

		// A world-space box taken into each joint's own skin space, grown by a_slack
		// world units: a point lies in the box when its bind position, under its
		// heaviest joint, lies in that joint's box.
		void JointBoxes(const std::vector<RE::NiTransform>& a_world, const Region& a_region, float a_slack, std::vector<Box>& a_out)
		{
			a_out.resize(a_world.size());
			const std::array<RE::NiPoint3, 8> corners{
				RE::NiPoint3{ a_region.low.x, a_region.low.y, a_region.low.z },
				RE::NiPoint3{ a_region.high.x, a_region.low.y, a_region.low.z },
				RE::NiPoint3{ a_region.low.x, a_region.high.y, a_region.low.z },
				RE::NiPoint3{ a_region.high.x, a_region.high.y, a_region.low.z },
				RE::NiPoint3{ a_region.low.x, a_region.low.y, a_region.high.z },
				RE::NiPoint3{ a_region.high.x, a_region.low.y, a_region.high.z },
				RE::NiPoint3{ a_region.low.x, a_region.high.y, a_region.high.z },
				RE::NiPoint3{ a_region.high.x, a_region.high.y, a_region.high.z },
			};
			for (std::size_t j = 0; j < a_world.size(); ++j) {
				const RE::NiTransform inverse = a_world[j].Invert();
				Box                   box{ inverse * corners[0], inverse * corners[0] };
				for (std::size_t c = 1; c < corners.size(); ++c) {
					const RE::NiPoint3 p = inverse * corners[c];
					box.low = { std::min(box.low.x, p.x), std::min(box.low.y, p.y), std::min(box.low.z, p.z) };
					box.high = { std::max(box.high.x, p.x), std::max(box.high.y, p.y), std::max(box.high.z, p.z) };
				}
				const float slack = a_slack / std::max(a_world[j].scale, 1e-3f);
				box.low -= RE::NiPoint3{ slack, slack, slack };
				box.high += RE::NiPoint3{ slack, slack, slack };
				a_out[j] = box;
			}
		}

		bool Inside(const Box& a_box, const RE::NiPoint3& a_point)
		{
			return a_point.x >= a_box.low.x && a_point.y >= a_box.low.y && a_point.z >= a_box.low.z && a_point.x <= a_box.high.x &&
			       a_point.y <= a_box.high.y && a_point.z <= a_box.high.z;
		}

		void Pose(Head& a_head, const Region* a_region, const Region* a_watch)
		{
			auto&      points = a_head.points;
			const bool compare = a_head.rest.size() == points.size();

			// The facegen dynamic data is the face's current shape, expression included.
			for (auto& dynamic : a_head.dynamics) {
				auto* shape = dynamic.geometry ? dynamic.geometry->AsDynamicTriShape() : nullptr;
				auto* skin = shape ? shape->GetGeometryRuntimeData().skinInstance.get() : nullptr;
				auto* partition = skin ? skin->skinPartition.get() : nullptr;
				if (!partition) {
					continue;
				}
				auto&               runtime = shape->GetDynamicTrishapeRuntimeData();
				RE::BSSpinLockGuard guard(runtime.lock);
				if (!runtime.dynamicData || runtime.dataSize < partition->vertexCount * kDynamicStride) {
					continue;
				}
				const auto* source = static_cast<const std::uint8_t*>(runtime.dynamicData);
				for (const auto& [point, vertex] : dynamic.points) {
					std::array<float, 3> xyz{};
					std::memcpy(xyz.data(), source + static_cast<std::size_t>(vertex) * kDynamicStride, sizeof(xyz));
					points[point].position = { xyz[0], xyz[1], xyz[2] };
				}
			}

			auto& world = a_head.world;
			world.assign(a_head.joints.size(), RE::NiTransform{});
			for (std::size_t j = 0; j < a_head.joints.size(); ++j) {
				const auto& joint = a_head.joints[j];
				if (joint.bone) {
					world[j] = joint.bone->world * joint.skinToBone;
				}
			}

			// Which points to pose: the ones around the ducts, and the ones whose
			// bind position can land in the region. The region is taken into each
			// joint's own space, so the test on a point is a box test on its bind
			// position under its heaviest joint; nearly every face point is weighted
			// to the head bone alone, and the slack covers the rest.
			auto& candidates = a_head.candidates;
			candidates.clear();
			auto& stamp = a_head.stamp;
			if (stamp.size() != points.size()) {
				stamp.assign(points.size(), 0);
			}
			for (const auto k : a_head.aroundDucts) {
				stamp[k] = frame;
				candidates.push_back(k);
			}
			if (a_region) {
				JointBoxes(world, *a_region, kRegionSlack, a_head.boxes);
				for (std::size_t k = 0; k < points.size(); ++k) {
					if (stamp[k] == frame) {
						continue;
					}
					const auto& point = points[k];
					if (Inside(a_head.boxes[point.joints[point.dominant]], point.position)) {
						candidates.push_back(static_cast<std::uint32_t>(k));
					}
				}
			}

			auto& patches = a_head.patches;
			patches.resize(candidates.size());
			float scale = 1.0f;
			for (std::size_t i = 0; i < candidates.size(); ++i) {
				SkinPoint(a_head, points[candidates[i]], patches[i].position, patches[i].normal, scale);
			}

			// Did an expression move the skin under the tears since last frame? Only the
			// dynamic points can move, so only they are tested, in bind space, which
			// needs no posing; the watch box is the tears themselves, so a blink that
			// moves the lids does not stir a trail further down the cheek.
			a_head.morphing = false;
			if (compare) {
				if (a_watch) {
					JointBoxes(world, *a_watch, kWatchSlack, a_head.watchBoxes);
					for (const auto& dynamic : a_head.dynamics) {
						for (const auto& [point, vertex] : dynamic.points) {
							const auto& p = points[point];
							if (p.position.GetSquaredDistance(a_head.rest[point]) > kMorphStep * kMorphStep &&
								Inside(a_head.watchBoxes[p.joints[p.dominant]], p.position)) {
								a_head.morphing = true;
								break;
							}
						}
						if (a_head.morphing) {
							break;
						}
					}
				}
				for (const auto& dynamic : a_head.dynamics) {
					for (const auto& [point, vertex] : dynamic.points) {
						a_head.rest[point] = points[point].position;
					}
				}
			} else {
				a_head.rest.resize(points.size());
				for (std::size_t k = 0; k < points.size(); ++k) {
					a_head.rest[k] = points[k].position;
				}
			}

			const RE::NiPoint3 gravity{ 0.0f, 0.0f, -1.0f };
			const auto         poseSpot = [&](std::size_t a_index, Duct& a_out) {
				a_out = {};
				if (a_index == kNone || a_index >= points.size()) {
					return;
				}
				SkinPoint(a_head, points[a_index], a_out.point, a_out.normal, scale);
				a_out.down = Unit(gravity - a_out.normal * gravity.Dot(a_out.normal), gravity);
				a_out.valid = true;
				a_head.scale = scale;
			};
			for (std::size_t e = 0; e < kEyes; ++e) {
				for (std::size_t s = 0; s < kMaxDucts; ++s) {
					poseSpot(a_head.duct[e][s], a_head.ducts[e][s]);
				}
				for (std::size_t row = 0; row < kLidRows; ++row) {
					for (std::size_t i = 0; i < kLidPoints; ++i) {
						poseSpot(a_head.lid[e][row][i], a_head.lids[e][row][i]);
					}
				}
			}
		}

		// a_placement only for its overrides; null keeps the ones the head was built with.
		Head* HeadOf(RE::Actor* a_actor, const Placement* a_placement = nullptr)
		{
			auto* root = a_actor ? a_actor->Get3D(false) : nullptr;
			if (!root || !root->parent) {
				return nullptr;
			}
			auto& head = heads[a_actor->GetHandle().native_handle()];
			head.seen = clock;
			const bool fresh = head.root.get() != root;
			const bool retune = a_placement && (a_placement->forward != head.forwardOverride || a_placement->eyesSpace != head.eyesSpace);
			if (!fresh && !retune && clock - head.checked < kRecheck) {
				return &head;
			}
			if (fresh) {
				Dull(head);  // the eyes material may outlive the old 3D
				head = Head{};
				head.root.reset(root);
				head.seen = clock;
			}
			head.checked = clock;
			std::vector<std::pair<const void*, std::uint32_t>> signature;
			for (auto type : { RE::BGSHeadPart::HeadPartType::kFace, RE::BGSHeadPart::HeadPartType::kEyes }) {
				auto* geometry = HeadPart(a_actor, root, type);
				geometry = geometry ? geometry : Guess(root, type == RE::BGSHeadPart::HeadPartType::kEyes);
				signature.emplace_back(geometry, VerticesOf(geometry));
			}
			if (fresh || retune || signature != head.signature || !head.ok) {
				const std::string before = head.report;
				head.signature = std::move(signature);
				if (a_placement) {
					head.forwardOverride = a_placement->forward;
					head.eyesSpace = a_placement->eyesSpace;
				}
				Build(a_actor, head);
				if (head.report == before) {
					return &head;
				}
				logger::info("{}: {}{}{}", a_actor->GetDisplayFullName(), head.report, head.ok ? "; face " + head.faceName : std::string{},
					head.ok ? "; eyes " + head.eyesName : std::string{});
			}
			return &head;
		}

		bool SameRegion(const Head& a_head, const Region* a_region)
		{
			if (!a_region) {
				return !a_head.regionPosed;
			}
			return a_head.regionPosed && a_head.region.low == a_region->low && a_head.region.high == a_region->high;
		}
	}

	void BeginFrame(float a_delta)
	{
		++frame;
		clock += a_delta;
		for (auto it = heads.begin(); it != heads.end();) {
			auto&      head = it->second;
			const bool gone = clock - head.seen > kForget || !head.root || !head.root->parent;
			if (gone || (head.glistening && clock > head.glistenUntil)) {
				Dull(head);
			}
			it = gone ? heads.erase(it) : std::next(it);
		}
	}

	void Glisten(RE::Actor* a_actor, float a_factor)
	{
		auto* head = HeadOf(a_actor);
		if (!head) {
			return;
		}
		if (!(a_factor > 1.001f)) {
			Dull(*head);
			return;
		}
		auto* eye = OwnEyeMaterial(*head);
		if (!eye) {
			return;
		}
		if (!head->glistening) {
			head->eyeEnv = eye->envMapScale;
			head->eyeSpec = eye->specularColorScale;
			head->glistening = true;
		}
		eye->envMapScale = head->eyeEnv * a_factor;
		eye->specularColorScale = head->eyeSpec * a_factor;
		head->glistenUntil = clock + 0.5f;
	}

	std::optional<View> Of(RE::Actor* a_actor, const Placement& a_placement, const Region* a_region, const Region* a_watch)
	{
		auto* head = HeadOf(a_actor, &a_placement);
		if (!head || !head->ok) {
			return std::nullopt;
		}
		if (head->placement != a_placement) {
			PlaceDucts(*head, a_placement);
		}
		if (head->frame != frame || !SameRegion(*head, a_region)) {
			// A second pose in one frame (a wider region after tears woke) sees no new
			// motion, since the first already took the rest positions; keep its verdict.
			const bool morphed = head->frame == frame && head->morphing;
			head->frame = frame;
			head->regionPosed = a_region != nullptr;
			if (a_region) {
				head->region = *a_region;
			}
			Pose(*head, a_region, a_watch);
			head->morphing = head->morphing || morphed;
		}
		return View{ head->patches, head->ducts, head->scale, head->morphing, head->lids };
	}

	std::string Describe(RE::Actor* a_actor, const Placement& a_placement)
	{
		auto* head = HeadOf(a_actor, &a_placement);
		if (!head) {
			return "no 3D loaded";
		}
		if (!head->ok) {
			return head->report;
		}
		if (head->placement != a_placement) {
			PlaceDucts(*head, a_placement);
		}
		const auto ducts = [&](std::size_t a_eye) {
			return std::ranges::count_if(head->duct[a_eye], [](std::size_t a_index) { return a_index != kNone; });
		};
		// The geometry behind the placement, for a report of tears in the wrong place:
		// where the eyes were measured, the face's centre and which way it was taken to
		// look, all in head-bone space (game units). A healthy head has the eyes a few
		// units apart on x, in front of the face centre on y and above it on z.
		const auto eye = [&](std::size_t a_eye) {
			const auto& shape = head->eyes[a_eye];
			return shape.valid ? std::format("({:.1f}, {:.1f}, {:.1f}) r {:.2f}", shape.center.x, shape.center.y, shape.center.z, shape.radius) : std::string("not found");
		};
		return std::format("face {}, eyes {}; {}; left ducts {}, right ducts {} (of {} streams); left eye {}, right eye {}, face centre ({:.1f}, {:.1f}, {:.1f}), forward {:+.0f}y",
			head->faceName, head->eyesName, head->report, ducts(kLeft), ducts(kRight), std::clamp(a_placement.streams, 1, static_cast<int>(kMaxStreams)),
			eye(kLeft), eye(kRight), head->faceCentre.x, head->faceCentre.y, head->faceCentre.z, head->forward);
	}

	void Reset()
	{
		for (auto& [key, head] : heads) {
			Dull(head);
		}
		heads.clear();
	}
}
