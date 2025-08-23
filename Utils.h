#ifndef __Utils_h__
#define __Utils_h__

#include <cmath>
#include <PANDUMatrix44.h>

class Utils
{
public:
	static constexpr float INFINITE_FAR_PLANE_ADJUST = 0.00001f;
	static constexpr float PI = 3.14159265359f;

	static float Radians(float Degrees)
	{
		return Degrees * PI / 180.0f;
	}

	static Pandu::Matrix44 GetProjectionMatrix(float FovY, float AspectRatio, float NearDist, float FarDist)
	{
		Pandu::Matrix44 ProjectionMatrix = Pandu::Matrix44::IDENTITY;

		const float tanThetaY = tan(FovY * 0.5f);

		const float tanThetaX = tanThetaY * AspectRatio;

		const float half_w = tanThetaX * NearDist;
		const float half_h = tanThetaY * NearDist;

		const float Left = -half_w;
		const float Right = +half_w;
		const float Bottom = -half_h;
		const float Top = +half_h;

		float inv_w = 1.0f / (Right - Left);
		float inv_h = 1.0f / (Top - Bottom);
		float inv_d = 1.0f / (FarDist - NearDist);


		// Calc matrix elements
		float A = 2 * NearDist * inv_w;
		float B = 2 * NearDist * inv_h;
		float C = (Right + Left) * inv_w;
		float D = (Top + Bottom) * inv_h;
		float q, qn;
		if (FarDist == 0)
		{
			// Infinite far plane
			q = INFINITE_FAR_PLANE_ADJUST - 1.0f;
			qn = NearDist * (INFINITE_FAR_PLANE_ADJUST - 2.0f);
		}
		else
		{
			q = -(FarDist + NearDist) * inv_d;
			qn = -2 * (FarDist * NearDist) * inv_d;
		}

		// NB: This creates 'uniform' perspective projection matrix,
		// which depth range [-1,1], right-handed rules
		//
		// [ A   0   C   0  ]
		// [ 0   B   D   0  ]
		// [ 0   0   q   qn ]
		// [ 0   0   -1  0  ]
		//
		// A = 2 * near / (right - left)
		// B = 2 * near / (top - bottom)
		// C = (right + left) / (right - left)
		// D = (top + bottom) / (top - bottom)
		// q = - (far + near) / (far - near)
		// qn = - 2 * (far * near) / (far - near)

		ProjectionMatrix[0][0] = A;
		ProjectionMatrix[0][2] = C;
		ProjectionMatrix[1][1] = B;
		ProjectionMatrix[1][2] = D;
		ProjectionMatrix[2][2] = q;
		ProjectionMatrix[2][3] = qn;
		ProjectionMatrix[3][2] = -1.0f;

		return ProjectionMatrix;
	}

	static void GetWebGPUMatrix(std::array<float, 16>& OutWebGPUMatrix, const Pandu::Matrix44& Matrix)
	{
		const Pandu::Matrix44 Transpose = Matrix.GetTranspose();

		std::copy(&Transpose.m[0][0], (&Transpose.m[0][0]) + 16, OutWebGPUMatrix.begin());
	}

};

#endif //  __Utils_h__
