#include <memory>
#include <cstdint>
#include <functional>
#include <string>
#include <cassert>
#include <vector>

#include "stb_image.hpp"
#include "stb_image_write.hpp"

typedef uint8_t u8;
typedef uint32_t u32;

struct Image {
	int width, height;
	std::unique_ptr<u8, std::function<void(u8*)>> data;

	Image() :
		width(-1), height(-1)
	{}

	Image(const std::string& filename) :
		data(stbi_load(filename.c_str(), &width, &height, nullptr, 4), stbi_image_free)
	{}

	Image& operator= (Image&& o) {
		width = o.width;
		height = o.height;
		data.swap(o.data);

		return *this;
	}

private:
	Image& operator= (const Image&);
};

struct Cubemap {
	enum CubeFace {
		FACE_POS_X, FACE_NEG_X,
		FACE_POS_Y, FACE_NEG_Y,
		FACE_POS_Z, FACE_NEG_Z,
		NUM_FACES
	};

	Image faces[NUM_FACES];

	Cubemap(const std::string& fname_prefix, const std::string& fname_extension) {
		faces[FACE_POS_X] = Image(fname_prefix + "_right."  + fname_extension);
		faces[FACE_NEG_X] = Image(fname_prefix + "_left."   + fname_extension);
		faces[FACE_POS_Y] = Image(fname_prefix + "_top."    + fname_extension);
		faces[FACE_NEG_Y] = Image(fname_prefix + "_bottom." + fname_extension);
		faces[FACE_POS_Z] = Image(fname_prefix + "_front."  + fname_extension);
		faces[FACE_NEG_Z] = Image(fname_prefix + "_back."   + fname_extension);
	}

	u32 readTexel(CubeFace face, int x, int y) {
		assert(face < NUM_FACES);
		const Image& face_img = faces[face];

		assert(x < face_img.width);
		assert(y < face_img.height);

		const u32* img_data = reinterpret_cast<const u32*>(face_img.data.get());
		return img_data[y * face_img.width + x];
	}

	void computeTexCoords(float x, float y, float z, CubeFace& out_face, float& out_s, float& out_t) {
		int major_axis;

		float v[3] = { x, y, z };
		float a[3] = { std::abs(x), std::abs(y), std::abs(z) };

		if (a[0] >= a[1] && a[0] >= a[2]) {
			major_axis = 0;
		} else if (a[1] >= a[0] && a[1] >= a[2]) {
			major_axis = 1;
		} else if (a[2] >= a[0] && a[2] >= a[1]) {
			major_axis = 2;
		}

		if (v[major_axis] < 0.0f)
			major_axis = major_axis * 2 + 1;
		else
			major_axis *= 2;

		float tmp_s, tmp_t, m;
		switch (major_axis) {
			/* +X */ case 0: tmp_s = -z; tmp_t = -y; m = a[0]; break;
			/* -X */ case 1: tmp_s =  z; tmp_t = -y; m = a[0]; break;
			/* +Y */ case 2: tmp_s =  x; tmp_t =  z; m = a[1]; break;
			/* -Y */ case 3: tmp_s =  x; tmp_t = -z; m = a[1]; break;
			/* +Z */ case 4: tmp_s =  x; tmp_t = -y; m = a[2]; break;
			/* -Z */ case 5: tmp_s = -x; tmp_t = -y; m = a[2]; break;
		}

		out_face = CubeFace(major_axis);
		out_s = 0.5f * (tmp_s / m + 1.0f);
		out_t = 0.5f * (tmp_t / m + 1.0f);
	}

	u32 sampleFace(CubeFace face, float s, float t) {
		const Image& face_img = faces[face];

		// Point sampling for now
		int x = std::min(static_cast<int>(s * face_img.width ), face_img.width  - 1);
		int y = std::min(static_cast<int>(t * face_img.height), face_img.height - 1);
		return readTexel(face, x, y);
	}
};

inline float unlerp(int val, int max) {
	return (val + 0.5f) / max;
}

int main(int argc, char* argv[]) {
	if (argc != 4)
		return 1;

	std::string fname_prefix = argv[1];
	std::string fname_extension = argv[2];
	int output_size = std::stoi(argv[3]);
	std::string output_fname = fname_prefix + "_spheremap.bmp";

	std::vector<u32> out_data(output_size * output_size);

	{
		Cubemap input_cubemap(fname_prefix, fname_extension);

		for (int y = 0; y < output_size; ++y) {
			for (int x = 0; x < output_size; ++x) {
				float s = unlerp(x, output_size);
				float t = unlerp(y, output_size);

				float vx, vy, vz;
				float rev_p = 16.0f * (s - s*s + t - t*t) - 4.0f;
				if (rev_p < 0.0f) {
					vx = 0.0f;
					vy = 0.0f;
					vz = -1.0f;
				} else {
					float rev_p_sqrt = std::sqrtf(rev_p);
					vx = rev_p_sqrt * (2.0f * s - 1.0f);
					vy = rev_p_sqrt * -(2.0f * t - 1.0f);
					vz = 8.0f * (s - s*s + t - t*t) - 3.0f;
				}

				Cubemap::CubeFace cube_face;
				float tex_s, tex_t;
				input_cubemap.computeTexCoords(vx, vy, vz, cube_face, tex_s, tex_t);

				out_data[y * output_size + x] = input_cubemap.sampleFace(cube_face, tex_s, tex_t);
			}
		}
	}

	stbi_write_bmp(output_fname.c_str(), output_size, output_size, 4, out_data.data());
}
