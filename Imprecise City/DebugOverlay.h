#pragma once

#include "../nclgl/OGLRenderer.h"
#include "../nclgl/Shader.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

#pragma comment(lib, "gdi32.lib")

class DebugOverlay {
public:
	~DebugOverlay() {
		glDeleteTextures(1, &fontTexture);
		glDeleteBuffers(1, &vbo);
		glDeleteVertexArrays(1, &vao);
	}

	bool Create(int screenHeight) {
		shader = std::make_unique<Shader>("overlayVertex.glsl", "overlayFragment.glsl");
		if (!shader->LoadSuccess() || !BuildFontAtlas(screenHeight)) {
			return false;
		}

		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, x));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, u));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, r));
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		const GLuint program = shader->GetProgram();
		screenLoc  = glGetUniformLocation(program, "screenSize");
		textureLoc = glGetUniformLocation(program, "useTexture");
		return true;
	}

	Shader& GetShader()       { return *shader; }          // the caller binds this before Draw()
	float   CharWidth() const { return (float)cellW; }
	float   LineHeight() const { return (float)cellH + 2.0f; }

	void Begin(int screenWidth, int screenHeight) {
		screenW = screenWidth;
		screenH = screenHeight;
		panelVerts.clear();
		textVerts.clear();
	}

	// Pixel coordinates, origin top-left.
	void Panel(float x, float y, float w, float h, const Vector4& colour) {
		AddQuad(panelVerts, x, y, w, h, 0.0f, 0.0f, 0.0f, 0.0f, colour);
	}

	void Text(float x, float y, const char* text, const Vector4& colour) {
		x = std::floor(x);   // whole pixels keep the glyphs crisp
		y = std::floor(y);
		for (const char* p = text; *p; ++p) {
			const int code = (unsigned char)*p;
			if (code > 32 && code < 128) {
				const int   index = code - 32;
				const float u0 = (float)((index % kColumns) * cellW) / atlasW;
				const float v0 = (float)((index / kColumns) * cellH) / atlasH;
				AddQuad(textVerts, x, y, (float)cellW, (float)cellH, u0, v0, u0 + (float)cellW / atlasW, v0 + (float)cellH / atlasH, colour);
			}
			x += (float)cellW;
		}
	}

	void Draw() {
		if (panelVerts.empty() && textVerts.empty()) {
			return;
		}
		const GLboolean cullWasOn = glIsEnabled(GL_CULL_FACE);
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glUniform2f(screenLoc, (float)screenW, (float)screenH);
		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D, fontTexture);

		const GLsizeiptr panelBytes = (GLsizeiptr)(panelVerts.size() * sizeof(Vertex));
		const GLsizeiptr textBytes  = (GLsizeiptr)(textVerts.size() * sizeof(Vertex));
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, panelBytes + textBytes, nullptr, GL_STREAM_DRAW);   // orphan the old storage: no stall
		if (panelBytes) { glBufferSubData(GL_ARRAY_BUFFER, 0, panelBytes, panelVerts.data()); }
		if (textBytes)  { glBufferSubData(GL_ARRAY_BUFFER, panelBytes, textBytes, textVerts.data()); }

		glUniform1f(textureLoc, 0.0f);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)panelVerts.size());
		glUniform1f(textureLoc, 1.0f);
		glDrawArrays(GL_TRIANGLES, (GLint)panelVerts.size(), (GLsizei)textVerts.size());

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		if (cullWasOn) { glEnable(GL_CULL_FACE); }
	}

private:
	struct Vertex {
		float x, y;      // screen pixels
		float u, v;      // font atlas
		float r, g, b, a;
	};

	static const int kColumns = 16;   // atlas layout: 16 x 6 cells = ASCII 32..127

	void AddQuad(std::vector<Vertex>& out, float x, float y, float w, float h,
	             float u0, float v0, float u1, float v1, const Vector4& c) {
		const Vertex a = { x,     y,     u0, v0, c.x, c.y, c.z, c.w };
		const Vertex b = { x + w, y,     u1, v0, c.x, c.y, c.z, c.w };
		const Vertex d = { x + w, y + h, u1, v1, c.x, c.y, c.z, c.w };
		const Vertex e = { x,     y + h, u0, v1, c.x, c.y, c.z, c.w };
		out.push_back(a); out.push_back(b); out.push_back(d);
		out.push_back(a); out.push_back(d); out.push_back(e);
	}

	bool BuildFontAtlas(int screenHeight) {
		const int fontHeight = std::max(14, screenHeight / 62);
		HDC dc = CreateCompatibleDC(nullptr);
		if (!dc) {
			return false;
		}
		HFONT font = CreateFontA(-fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
		                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		                         FIXED_PITCH | FF_MODERN, "Consolas");
		if (!font) {
			DeleteDC(dc);
			return false;
		}
		HGDIOBJ oldFont = SelectObject(dc, font);

		SIZE extent = {};
		GetTextExtentPoint32A(dc, "M", 1, &extent);   // a monospace font: every cell is as wide as 'M'
		cellW = std::max(1, (int)extent.cx);
		cellH = std::max(1, (int)extent.cy);
		const int rows = 96 / kColumns;
		const int aw   = kColumns * cellW;
		const int ah   = rows * cellH;
		atlasW = (float)aw;
		atlasH = (float)ah;

		BITMAPINFO info = {};
		info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth       = aw;
		info.bmiHeader.biHeight      = -ah;   // negative = top-down rows, same order as our UVs
		info.bmiHeader.biPlanes      = 1;
		info.bmiHeader.biBitCount    = 32;
		info.bmiHeader.biCompression = BI_RGB;
		void*   bits   = nullptr;
		HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!bitmap || !bits) {
			SelectObject(dc, oldFont);
			DeleteObject(font);
			DeleteDC(dc);
			return false;
		}
		HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

		std::memset(bits, 0, (size_t)aw * ah * 4);
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, RGB(255, 255, 255));
		for (int i = 0; i < kColumns * rows; ++i) {
			const char c = (char)(32 + i);
			TextOutA(dc, (i % kColumns) * cellW, (i / kColumns) * cellH, &c, 1);
		}
		GdiFlush();

		std::vector<unsigned char> coverage((size_t)aw * ah);
		const unsigned char* src = (const unsigned char*)bits;
		for (size_t n = 0; n < coverage.size(); ++n) {
			coverage[n] = src[n * 4 + 1];   // grey anti-aliasing: all three colour channels are equal
		}

		SelectObject(dc, oldBitmap);
		SelectObject(dc, oldFont);
		DeleteObject(bitmap);
		DeleteObject(font);
		DeleteDC(dc);

		glGenTextures(1, &fontTexture);
		glBindTexture(GL_TEXTURE_2D, fontTexture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);   // rows of 1-byte pixels are not 4-byte aligned in general
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, aw, ah, 0, GL_RED, GL_UNSIGNED_BYTE, coverage.data());
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		return true;
	}

	std::unique_ptr<Shader> shader;
	GLuint fontTexture = 0, vao = 0, vbo = 0;
	GLint  screenLoc = -1, textureLoc = -1;
	int    cellW = 8, cellH = 16;       // size of one glyph cell in pixels
	float  atlasW = 1.0f, atlasH = 1.0f;
	int    screenW = 1, screenH = 1;
	std::vector<Vertex> panelVerts, textVerts;
};
