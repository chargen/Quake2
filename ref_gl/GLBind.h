// Autogenerated file: do not modify

struct GL_t
{
	void	(APIENTRY * AlphaFunc) (GLenum func, GLclampf ref);
	void	(APIENTRY * Begin) (GLenum mode);
	void	(APIENTRY * BindTexture) (GLenum target, GLuint texture);
	void	(APIENTRY * BlendFunc) (GLenum sfactor, GLenum dfactor);
	void	(APIENTRY * Clear) (GLbitfield mask);
	void	(APIENTRY * ClearColor) (GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
	void	(APIENTRY * Color3f) (GLfloat red, GLfloat green, GLfloat blue);
	void	(APIENTRY * Color3fv) (const GLfloat *v);
	void	(APIENTRY * Color4f) (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
	void	(APIENTRY * Color4ubv) (const GLubyte *v);
	void	(APIENTRY * ColorPointer) (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
	void	(APIENTRY * CullFace) (GLenum mode);
	void	(APIENTRY * DeleteTextures) (GLsizei n, const GLuint *textures);
	void	(APIENTRY * DepthFunc) (GLenum func);
	void	(APIENTRY * DepthMask) (GLboolean flag);
	void	(APIENTRY * DepthRange) (GLclampd zNear, GLclampd zFar);
	void	(APIENTRY * Disable) (GLenum cap);
	void	(APIENTRY * DisableClientState) (GLenum array);
	void	(APIENTRY * DrawElements) (GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
	void	(APIENTRY * Enable) (GLenum cap);
	void	(APIENTRY * EnableClientState) (GLenum array);
	void	(APIENTRY * End) ();
	void	(APIENTRY * Finish) ();
	void	(APIENTRY * Fogf) (GLenum pname, GLfloat param);
	void	(APIENTRY * Fogfv) (GLenum pname, const GLfloat *params);
	GLenum	(APIENTRY * GetError) ();
	void	(APIENTRY * GetIntegerv) (GLenum pname, GLint *params);
	const GLubyte *	(APIENTRY * GetString) (GLenum name);
	void	(APIENTRY * LoadIdentity) ();
	void	(APIENTRY * LoadMatrixf) (const GLfloat *m);
	void	(APIENTRY * MatrixMode) (GLenum mode);
	void	(APIENTRY * Ortho) (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
	void	(APIENTRY * PolygonMode) (GLenum face, GLenum mode);
	void	(APIENTRY * PolygonOffset) (GLfloat factor, GLfloat units);
	void	(APIENTRY * PopMatrix) ();
	void	(APIENTRY * PushMatrix) ();
	void	(APIENTRY * ReadPixels) (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
	void	(APIENTRY * Rotatef) (GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
	void	(APIENTRY * Scissor) (GLint x, GLint y, GLsizei width, GLsizei height);
	void	(APIENTRY * ShadeModel) (GLenum mode);
	void	(APIENTRY * TexCoord2f) (GLfloat s, GLfloat t);
	void	(APIENTRY * TexCoordPointer) (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
	void	(APIENTRY * TexEnvf) (GLenum target, GLenum pname, GLfloat param);
	void	(APIENTRY * TexEnvfv) (GLenum target, GLenum pname, const GLfloat *params);
	void	(APIENTRY * TexEnvi) (GLenum target, GLenum pname, GLenum param);
	void	(APIENTRY * TexImage2D) (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
	void	(APIENTRY * TexParameteri) (GLenum target, GLenum pname, GLint param);
	void	(APIENTRY * TexSubImage2D) (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
	void	(APIENTRY * Translatef) (GLfloat x, GLfloat y, GLfloat z);
	void	(APIENTRY * Vertex2f) (GLfloat x, GLfloat y);
	void	(APIENTRY * Vertex3f) (GLfloat x, GLfloat y, GLfloat z);
	void	(APIENTRY * Vertex3fv) (const GLfloat *v);
	void	(APIENTRY * VertexPointer) (GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
	void	(APIENTRY * Viewport) (GLint x, GLint y, GLsizei width, GLsizei height);

#if _WIN32

	int	(APIENTRY * ChoosePixelFormat) (HDC hdc, CONST PIXELFORMATDESCRIPTOR *ppfd);
	int	(APIENTRY * DescribePixelFormat) (HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd);
	BOOL	(APIENTRY * SetPixelFormat) (HDC hdc, int iPixelFormat, CONST PIXELFORMATDESCRIPTOR *ppfd);
	BOOL	(APIENTRY * SwapBuffers) (HDC hdc);
	HGLRC	(APIENTRY * CreateContext) (HDC hdc);
	BOOL	(APIENTRY * DeleteContext) (HGLRC hglrc);
	PROC	(APIENTRY * GetProcAddress) (LPCSTR lpszProc);
	BOOL	(APIENTRY * MakeCurrent) (HDC hdc, HGLRC hglrc);

#endif

	void	(APIENTRY * SelectTextureSGIS) (GLenum target);
	void	(APIENTRY * SelectTextureCoordSetSGIS) (GLenum target);
	void	(APIENTRY * ActiveTextureARB) (GLenum texture);
	void	(APIENTRY * ClientActiveTextureARB) (GLenum texture);
	void	(APIENTRY * LockArraysEXT) (GLint first, GLint count);
	void	(APIENTRY * UnlockArraysEXT) ();
	void	(APIENTRY * DrawRangeElementsEXT) (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices);

#if _WIN32

	BOOL	(APIENTRY * SwapIntervalEXT) (int interval);

#elif __linux__

#endif

};

extern GL_t GL;


#define glAlphaFunc	GL.AlphaFunc
#define glBegin	GL.Begin
#define glBindTexture	GL.BindTexture
#define glBlendFunc	GL.BlendFunc
#define glClear	GL.Clear
#define glClearColor	GL.ClearColor
#define glColor3f	GL.Color3f
#define glColor3fv	GL.Color3fv
#define glColor4f	GL.Color4f
#define glColor4ubv	GL.Color4ubv
#define glColorPointer	GL.ColorPointer
#define glCullFace	GL.CullFace
#define glDeleteTextures	GL.DeleteTextures
#define glDepthFunc	GL.DepthFunc
#define glDepthMask	GL.DepthMask
#define glDepthRange	GL.DepthRange
#define glDisable	GL.Disable
#define glDisableClientState	GL.DisableClientState
#define glDrawElements	GL.DrawElements
#define glEnable	GL.Enable
#define glEnableClientState	GL.EnableClientState
#define glEnd	GL.End
#define glFinish	GL.Finish
#define glFogf	GL.Fogf
#define glFogfv	GL.Fogfv
#define glGetError	GL.GetError
#define glGetIntegerv	GL.GetIntegerv
#define glGetString	GL.GetString
#define glLoadIdentity	GL.LoadIdentity
#define glLoadMatrixf	GL.LoadMatrixf
#define glMatrixMode	GL.MatrixMode
#define glOrtho	GL.Ortho
#define glPolygonMode	GL.PolygonMode
#define glPolygonOffset	GL.PolygonOffset
#define glPopMatrix	GL.PopMatrix
#define glPushMatrix	GL.PushMatrix
#define glReadPixels	GL.ReadPixels
#define glRotatef	GL.Rotatef
#define glScissor	GL.Scissor
#define glShadeModel	GL.ShadeModel
#define glTexCoord2f	GL.TexCoord2f
#define glTexCoordPointer	GL.TexCoordPointer
#define glTexEnvf	GL.TexEnvf
#define glTexEnvfv	GL.TexEnvfv
#define glTexEnvi	GL.TexEnvi
#define glTexImage2D	GL.TexImage2D
#define glTexParameteri	GL.TexParameteri
#define glTexSubImage2D	GL.TexSubImage2D
#define glTranslatef	GL.Translatef
#define glVertex2f	GL.Vertex2f
#define glVertex3f	GL.Vertex3f
#define glVertex3fv	GL.Vertex3fv
#define glVertexPointer	GL.VertexPointer
#define glViewport	GL.Viewport

#if _WIN32

#define wglChoosePixelFormat	GL.ChoosePixelFormat
#define wglDescribePixelFormat	GL.DescribePixelFormat
#define wglSetPixelFormat	GL.SetPixelFormat
#define wglSwapBuffers	GL.SwapBuffers
#define wglCreateContext	GL.CreateContext
#define wglDeleteContext	GL.DeleteContext
#define wglGetProcAddress	GL.GetProcAddress
#define wglMakeCurrent	GL.MakeCurrent

#endif

#define glSelectTextureSGIS	GL.SelectTextureSGIS
#define glSelectTextureCoordSetSGIS	GL.SelectTextureCoordSetSGIS
#define glActiveTextureARB	GL.ActiveTextureARB
#define glClientActiveTextureARB	GL.ClientActiveTextureARB
#define glLockArraysEXT	GL.LockArraysEXT
#define glUnlockArraysEXT	GL.UnlockArraysEXT
#define glDrawRangeElementsEXT	GL.DrawRangeElementsEXT

#if _WIN32

#define wglSwapIntervalEXT	GL.SwapIntervalEXT

#elif __linux__

#endif



#define QGL_SGIS_MULTITEXTURE	(1 << 0)
#define QGL_ARB_MULTITEXTURE	(1 << 1)
#define QGL_ARB_TEXTURE_ENV_ADD	(1 << 2)
#define QGL_EXT_TEXTURE_ENV_COMBINE	(1 << 3)
#define QGL_ARB_TEXTURE_ENV_COMBINE	(1 << 4)
#define QGL_NV_TEXTURE_ENV_COMBINE4	(1 << 5)
#define QGL_ATI_TEXTURE_ENV_COMBINE3	(1 << 6)
#define QGL_S3_S3TC	(1 << 7)
#define QGL_ARB_TEXTURE_COMPRESSION	(1 << 8)
#define QGL_EXT_TEXTURE_COMPRESSION_S3TC	(1 << 9)
#define QGL_EXT_COMPILED_VERTEX_ARRAY	(1 << 10)
#define QGL_EXT_DRAW_RANGE_ELEMENTS	(1 << 11)
#define QGL_NV_TEXTURE_RECTANGLE	(1 << 12)
#define QGL_EXT_TEXTURE_LOD_BIAS	(1 << 13)
#define QGL_EXT_TEXTURE_EDGE_CLAMP	(1 << 14)

#if _WIN32

#define QWGL_EXT_SWAP_CONTROL	(1 << 15)

#elif __linux__

#endif


