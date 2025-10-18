/*
 *  Copyright (c) 2000-2022 Inria
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *  * Neither the name of the ALICE Project-Team nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  Contact: Bruno Levy
 *
 *     https://www.inria.fr/fr/bruno-levy
 *
 *     Inria,
 *     Domaine de Voluceau,
 *     78150 Le Chesnay - Rocquencourt
 *     FRANCE
 *
 */

#include <geogram_gfx/gui/simple_application.h>
#include <geogram_gfx/GLUP/GLUP_private.h>
#include <geogram/basic/stopwatch.h>
#include "generateGeometry.h"
#include "uv.xpm"

namespace {
    using namespace GEO;

    /**
     * \brief Draws the sphere eversion.
     * \details Borrowed from Michael Mc Guffin's implementation.
     */

    class EvertableSphere {
    public:

        enum RenderingStyle {
            STYLE_POINTS,
            STYLE_POLYGONS,
            STYLE_CHECKERED,
            STYLE_BANDS
        };

        /**
         * \brief EvertableSphere constructor.
         */
        EvertableSphere() :
            vertices_dirty_(true) {
            rendering_style_ = STYLE_POLYGONS;
            show_half_strips_ = false;
            bend_cylinder_ = false;
            nb_strips_ = 8;
            nb_hemispheres_to_display_ = 2;
            nb_strips_to_display_ = nb_strips_;
            nb_lat_per_hemisphere_ = 100;
            nb_long_per_strip_ = 100;
            vertices_dirty_ = true;
            textured_ = false;
        }

        /**
         * \brief Draws the everting sphere with the current parameters.
         */
        void draw() {

	    quads_.begin_frame();

            // Update the vertices if needed.
            if (vertices_dirty_) {
                generate_vertices();
            }

            // Set the colors
            glupDisable(GLUP_VERTEX_COLORS);
            if ( rendering_style_ == STYLE_POINTS ) {
                glupSetColor4f(
		    GLUP_FRONT_AND_BACK_COLOR, 1.0f, 1.0f, 0.0f, alpha
		);
            } else {
                glupSetColor4f(GLUP_FRONT_COLOR, 0.0f, 0.5f, 1.0f, alpha);
                glupSetColor4f(GLUP_BACK_COLOR, 1.0f, 0.0f, 0.0f, alpha);
            }

            glupMatrixMode(GLUP_MODELVIEW_MATRIX);

            // Draw the two hemispheres.
            for (
                int hemisphere = 0;
                hemisphere < nb_hemispheres_to_display_; ++hemisphere
            ) {

                glupPushMatrix();
                glupRotatef(float(hemisphere)*180.0f,0.0f,1.0f,0.0f);

                // Draw the nb_strips_ strips in the current hemisphere.
                for (int strip = 0; strip < nb_strips_to_display_; ++strip) {
                    float angle = 0.0;
                    if(hemisphere == 0) {
                        angle = -float(strip)*360.0f / float(nb_strips_);
                    } else {
                        angle = float(strip+1)*360.0f / float(nb_strips_);
                    }
                    glupPushMatrix();
                    glupRotatef(angle, 0.0f,0.0f,1.0f);
                    draw_strip(hemisphere);
                    glupPopMatrix();
                }
                glupPopMatrix();
            }

	    quads_.end_frame();
        }

        /**
         * \brief Sets the eversion time.
         * \param[in] t the time, in [0.0, 1.0]. It is clamped if it is
         *  outside of the [0.0, 1.0] range.
         */
        void set_time(double t) {
            geo_clamp(t, 0.0, 1.0);
            if(time_ != t) {
                time_ = t;
                update();
            }
        }


        /**
         * \brief Sets the number of strips (or corrugations).
         * \param[in] n the number of strips. It is known that for n >= 8;
         *  the eversion is smooth. For lower values of n it is not necessarily
         *  the case.
         */
        void set_nb_strips(int n) {
            nb_strips_ = n;
            nb_strips_to_display_ = n;
            update();
        }


        /**
         * \brief Sets the number of hemispheres to display.
         * \param[in] nb the number of hemispheres to display, in {0,1,2}
         */
        void set_nb_hemispheres_to_display(int nb) {
            geo_clamp(nb, 0, 2);
            nb_hemispheres_to_display_ = nb;
        }

        /**
         * \brief Sets the number of strips to display.
         * \param[in] n the number of strips to display, in [0..nb_strips]
         */
        void set_nb_strips_to_display(int n) {
            geo_clamp(n, 0, nb_strips_);
            nb_strips_to_display_ = n;
        }

        /**
         * \brief Sets the latitudinal resolution.
         * \param[in] n the number of vertices along the latitudinal resolution.
         */
        void set_lat_resolution(int n) {
            if(nb_lat_per_hemisphere_ != n) {
                nb_lat_per_hemisphere_ = n;
                update();
            }
        }

        /**
         * \brief Sets the longitudinal resolution.
         * \param[in] n the number of vertices along the longitudinal
         *  resolution.
         */
        void set_lon_resolution(int n) {
            if(nb_long_per_strip_ != n) {
                nb_long_per_strip_ = n;
                update();
            }
        }

        /**
         * \brief Sets the rendering style.
         * \param[in] s one of STYLE_POINTS, STYLE_POLYGONS, STYLE_CHECKERED,
         *  STYLE_BANDS
         */
        void set_rendering_style(RenderingStyle s) {
            rendering_style_ = s;
        }

        /**
         * \brief Sets the opacity.
         * \param[in] a the opacity (0.0 for transparent, 0.5 for translucent
         *  and 1.0 for opaque).
         */
        void set_alpha(float a) {
            alpha = a;
        }

        /**
         * \brief Sets whether half strips should be displayed.
         * \param[in] h if set, only half strips are displayed. This lets see
         *  the internal structures between them.
         */
        void set_show_half_strips(bool h) {
            if(h != show_half_strips_) {
                show_half_strips_ = h;
                update();
            }
        }

        /**
         * \brief Sets whether a cylinder-to-sphere morph should be displayed
         *  instead of the sphere eversion.
         * \param[in] b if set, a cylinder-to-sphere morph is displayed, else
         *  the sphere eversion is displayed
         */
        void set_bend_cylinder(bool b) {
            if(bend_cylinder_ != b) {
                bend_cylinder_ = b;
                update();
            }
        }

        /**
         * \brief Sets whether texture coordinates should be generated.
         * \param[in] b true if texture coordinates should be generated,
         *  false otherwise
         */
        void set_textured(bool b) {
            textured_ = b;
        }

	void set_transparent(bool b) {
	    quads_.set_transparent(
		b && (rendering_style_ != STYLE_POINTS) && !textured_
	    );
	}

    protected:

        /**
         * \brief Indicates that the vertices array should be recomputed.
         */
        void update() {
            vertices_dirty_ = true;
        }

        /**
         * \brief Draws a strip (a corrugation).
         * \param[in] hemisphere the hemisphere in which the strip resides.
         */
        void draw_strip(int hemisphere) {
            if (rendering_style_ == STYLE_POINTS) {
                glupBegin(GLUP_POINTS);
                for (int j=0; j<=nb_lat_per_hemisphere_; ++j)
                    for (int k=0; k<=nb_long_per_strip_; ++k) {
                        draw_vertex(j,k);
                    }
                glupEnd();
            } else {
		quads_.begin();
                for (int j=0; j<nb_lat_per_hemisphere_; ++j) {
                    if (
                        rendering_style_ == STYLE_POLYGONS ||
                        (
                            rendering_style_ == STYLE_BANDS &&
                            (j & 1)==hemisphere
                        )
                    ) {
                        for(int k=0; k<nb_long_per_strip_; ++k) {
                            draw_vertex(j+1,k);
                            draw_vertex(j,k);
                            draw_vertex(j,k+1);
                            draw_vertex(j+1,k+1);
                        }
                    } else if (rendering_style_ == STYLE_CHECKERED) {
                        for(int k=j%2; k<nb_long_per_strip_; k+=2) {
                            draw_vertex(j+1,k);
                            draw_vertex(j,k);
                            draw_vertex(j,k+1);
                            draw_vertex(j+1,k+1);
                        }
                    }
                }
		quads_.end();
            }
        }

        /**
         * \brief Generates the vertices.
         */
        void generate_vertices() {
            geo_clamp(time_, 0.0, 1.0);

            if (nb_lat_per_hemisphere_ < 2) {
                nb_lat_per_hemisphere_ = 2;
            }

            if (nb_long_per_strip_ < 2) {
                nb_long_per_strip_ = 2;
            }

            // allocate vertices
            vertices_.resize(
                size_t(3*(1 + nb_lat_per_hemisphere_)*(1 + nb_long_per_strip_))
            );
            normals_.resize(
                size_t(3*(1 + nb_lat_per_hemisphere_)*(1 + nb_long_per_strip_))
            );

            //  Make a tiny invisible puncture near the pole, to avoid a
            // singularity that creates a bad shading.
            static const double epsilon = 1e-5;

            // generate the geometry
            if(bend_cylinder_) {
                generateGeometry(
                    vertices_.data(),
                    normals_.data(),
                    time_,
                    nb_strips_,
                    0.0 + epsilon,
                    nb_lat_per_hemisphere_,
                    1.0,
                    0.0,
                    nb_long_per_strip_,
                    (show_half_strips_ ? 0.5 : 1.0),
                    time_
                );
            } else  {
                generateGeometry(
                    vertices_.data(),
                    normals_.data(),
                    time_,
                    nb_strips_,
                    0.0 + epsilon,
                    nb_lat_per_hemisphere_,
                    1.0,
                    0.0,
                    nb_long_per_strip_,
                    show_half_strips_ ? 0.5 : 1.0
                );
            }
            vertices_dirty_ = false;
        }

        inline void draw_vertex(int u, int v) {
	    int lin_index = 3*(v * (nb_lat_per_hemisphere_ + 1) + u);
	    if(textured_) {
		quads_.TexCoord2f(
		    float(u) / float(nb_lat_per_hemisphere_),
		    float(v) / float(nb_long_per_strip_)
		);
	    }
	    quads_.Normal3fv(&normals_[lin_index]);
	    quads_.Vertex3fv(&vertices_[lin_index]);
        }

    protected:

	/**
	 * \brief Stores a list of quads and their normals
	 *  for sorted transparent rendering.
	 */
	class QuadsBuffer {
	public:
	    QuadsBuffer() : quads_pointer_(0), transparent_(false) {
	    }

	    void set_transparent(bool x) {
		transparent_ = x;
	    }

	    /**
	     * \brief Should be called at the beginning of each frame.
	     */
	    void begin_frame() {
		quads_pointer_ = 0;
		quads_.resize(0);
		quads_order_.resize(0);
	    }

	    /**
	     * \brief Starts a new QUADS primitive
	     */
	    void begin() {
		if(!transparent_) {
		    glupBegin(GLUP_QUADS);
		}
	    }

	    /**
	     * \brief Specifies the normal vector for the next vertex
	     * \details Should be called before Vertex3fv()
	     */
	    void Normal3fv(const float* N) {
		if(transparent_) {
		    quads_.emplace_back(vec3f(N));
		} else {
		    // (glupPrivateXXX are just faster inline versions
		    //  of glupXXX)
		    glupPrivateNormal3fv(const_cast<float*>(N));
		}
	    }

	    /**
	     * \brief Specifies the tex coords for the next vertex
	     * \details Should be called before Vertex3fv(). Ignored
	     *  in transparent mode.
	     */
	    void TexCoord2f(float u, float v) {
		if(!transparent_) {
		    // (glupPrivateXXX are just faster inline versions
		    //  of glupXXX)
		    glupPrivateTexCoord2f(u,v);
		}
	    }

	    /**
	     * \brief Draws a vertex
	     */
	    void Vertex3fv(const float* P) {
		if(transparent_) {
		    quads_.emplace_back(vec3f(P));
		} else {
		    // (glupPrivateXXX are just faster inline versions
		    //  of glupXXX)
		    glupPrivateVertex3fv(P);
		}
	    }

	    /**
	     * \brief terminates a QUADS primitive
	     */
	    void end() {
		if(!transparent_) {
		    glupEnd();
		    return;
		}

		// Applies the ModelView transform to the normals and
		// vertices stored since latest call to begin_frame() or begin()
		mat4 modelview_t;
		glupGetMatrixdv(GLUP_MODELVIEW_MATRIX, modelview_t.data());

		// matrix to be used for transforming points
		// transposed because OpenGL uses row vectors and
		// vector x matrix transforms.
		mat4 modelview = modelview_t.transpose();

		// matrix to be used for transforming normals
		// transposed transposed (that is, not transposed)
		// because, once agian, OpenGL uses row vectors and
		// vector x matrix transforms.
		mat4 modelview_inv_t = modelview_t.inverse();

		for(index_t i=quads_pointer_; i<quads_.size(); i+=2) {
		    vec4 p(
			double(quads_[i+1].x),
			double(quads_[i+1].y),
			double(quads_[i+1].z),
			1.0
		    );

		    vec4 p_xformed= mult(modelview, p);

		    vec4 n(
			double(quads_[i].x),
			double(quads_[i].y),
			double(quads_[i].z),
			0.0
		    );

		    vec4 n_xformed = mult(modelview_inv_t, n);

		    quads_[i+1].x = float(p_xformed.x / p_xformed.w);
		    quads_[i+1].y = float(p_xformed.y / p_xformed.w);
		    quads_[i+1].z = float(p_xformed.z / p_xformed.w);

		    quads_[i].x = float(n_xformed.x);
		    quads_[i].y = float(n_xformed.y);
		    quads_[i].z = float(n_xformed.z);
		}
		quads_pointer_ = quads_.size();
	    }

	    /**
	     * \brief needs to be called at the end of each frame.
	     * \details In transparent mode, sorts and draws all the stored
	     *  transparent quads in back to front order.
	     */
	    void end_frame() {

		if(!transparent_) {
		    return;
		}

		index_t nb_quads = quads_.size()/8;

		// sort the quads in back to front order
		quads_order_.resize(nb_quads);
		for(index_t i=0; i<quads_order_.size(); ++i) {
		    quads_order_[i] = i;
		}

		std::sort(
		    quads_order_.begin(), quads_order_.end(),
		    [this](index_t i, index_t j)->bool{
			// compares the avg depth of both quads
			float z1 =
			    quads_[8*i+1].z +
			    quads_[8*i+3].z +
			    quads_[8*i+5].z +
			    quads_[8*i+7].z ;
			float z2 =
			    quads_[8*j+1].z +
			    quads_[8*j+3].z +
			    quads_[8*j+5].z +
			    quads_[8*j+7].z ;
			return (z1 < z2);
		    }
		);

		// set ModelView matrix to identity (quads are
		// pre-transformed)
		glupMatrixMode(GLUP_MODELVIEW_MATRIX);
		glupPushMatrix();
		glupLoadIdentity();

		// draw the quads in back to front order
		// (glupPrivateXXX are just faster inline versions of glupXXX)
		glupBegin(GLUP_QUADS);
		for(index_t i=0; i<quads_order_.size(); i++) {
		    index_t q = quads_order_[i];
		    glupPrivateNormal3fv(quads_[8*q  ].data());
		    glupPrivateVertex3fv(quads_[8*q+1].data());
		    glupPrivateNormal3fv(quads_[8*q+2].data());
		    glupPrivateVertex3fv(quads_[8*q+3].data());
		    glupPrivateNormal3fv(quads_[8*q+4].data());
		    glupPrivateVertex3fv(quads_[8*q+5].data());
		    glupPrivateNormal3fv(quads_[8*q+6].data());
		    glupPrivateVertex3fv(quads_[8*q+7].data());
		}
		glupEnd();

		// restore ModelView matrix
		glupPopMatrix();
	    }

	private:
	    vector<vec3f> quads_;
	    vector<index_t> quads_order_;
	    index_t quads_pointer_;
	    bool transparent_;
	};

    private:
        double time_;

        int nb_strips_;
        int nb_hemispheres_to_display_;
        int nb_strips_to_display_;
        int nb_lat_per_hemisphere_;
        int nb_long_per_strip_;

        // Stores all the vertices used to render the sphere.
        // Elements in the array are arranged by [latitude][longitude][coord].
        vector<float> vertices_;
        vector<float> normals_;

        bool vertices_dirty_; // If true, need to regenerate vertices.

        bool show_half_strips_;

        RenderingStyle rendering_style_;
        float alpha;

        bool bend_cylinder_;
        bool textured_;

	QuadsBuffer quads_;
    };

    /**
     * \brief Porting Michael Mc Guffin's implementation
     *  of sphere eversion to GLUP.
     */
    class DemoEvertApplication : public SimpleApplication {
    public:


        /**
         * \brief DemoGlupApplication constructor.
         */
        DemoEvertApplication() : SimpleApplication("Evert") {
            // Define the 3d region that we want to display
            // (xmin, ymin, zmin, xmax, ymax, zmax)
            set_region_of_interest(-1.0, -1.0, -1.0, 1.0, 1.0, 1.0);

            style_ = EvertableSphere::STYLE_POLYGONS;
            transparent_ = false;
            alpha_ = 0.5f;
            half_sphere_ = false;
            half_strips_ = false;
            time_ = 0.0f;
            res_longitude_ = 40;
            res_latitude_ = 40;
            nb_strips_ = 8;
            anim_speed_ = 0.5f;
            proportion_strips_to_display_ = 1.0f;
            mesh_ = false;
            shrink_ = 0.0f;
            point_size_ = 10.0f;
            bend_cylinder_ = false;
            textured_ = false;
            texture_ = 0;
            smooth_ = true;
        }

        /**
         * \copydoc SimpleApplication::GL_terminate()
         */
        void GL_terminate() override {
            if(texture_ != 0) {
                glDeleteTextures(1,&texture_);
            }
            SimpleApplication::GL_terminate();
        }

        /**
         * \brief Displays and handles the GUI for object properties.
         * \details Overloads Application::draw_object_properties().
         */
        void draw_object_properties() override {
            SimpleApplication::draw_object_properties();

            ImGui::SliderFloat("spd.", &anim_speed_, 0.02f, 2.0f, "%.2f");
            ImGui::Tooltip("animation speed");

            ImGui::SliderFloat("time", &time_, 0.0f, 1.0f, "%.2f");

            ImGui::Combo("style", (int*)&style_,
                         "points\0polygons\0checkered\0bands\0\0"
                        );
            if(style_ == EvertableSphere::STYLE_POINTS) {
                ImGui::SliderFloat("ptsz", &point_size_, 1.0f, 20.0f, "%.1f");
                ImGui::Tooltip("point size");
            } else {
                ImGui::Checkbox("mesh", &mesh_);
                ImGui::SliderFloat("shrk", &shrink_, 0.0f, 1.0f, "%.2f");
                ImGui::Tooltip("polygons shrink");
            }

            ImGui::Checkbox("half sphere", &half_sphere_);
            ImGui::Tooltip("hide one half of the sphere");

            ImGui::Checkbox("half strips", &half_strips_);
            ImGui::Tooltip("hide one half of each corrugation");

            ImGui::SliderFloat(
                "prop", &proportion_strips_to_display_,
                0.0f, 1.0f, "%.2f"
            );
            ImGui::Tooltip("cheese-proportion of the corrugations to draw");

            ImGui::SliderInt("strp", &nb_strips_, 1, 50);
            ImGui::Tooltip(
                "number of corrugations \n"
                "(if <8, smoothness is not guaranteed)"
            );

            ImGui::SliderInt("lon.", &res_longitude_, 12, 200);
            ImGui::Tooltip("number of longitudinal subdivisions");

            ImGui::SliderInt("lat.", &res_latitude_, 12, 200);
            ImGui::Tooltip("number of latitudinal subdivisions");

            if(ImGui::Checkbox("textured", &textured_)) {
                sphere_.set_textured(textured_);
            }

	    ImGui::Checkbox("transparent", &transparent_);
	    if(transparent_) {
		ImGui::SliderFloat("opac.", &alpha_, 0.0f, 1.0f, "%.2f");
	    }
	    sphere_.set_transparent(transparent_);

            ImGui::Checkbox("cylinder", &bend_cylinder_);
            ImGui::Tooltip(
                "display sphere<->cylinder morph\n"
                "instead of sphere eversion\n"
                "(not as cool, but cool enough)\n"
            );
            ImGui::Checkbox("smooth", &smooth_);
        }

        /**
         * \brief Creates the texture.
         * \details This function overloads Application::init_graphics(). It
         *  is called as soon as the OpenGL context is ready for rendering. It
         *  is meant to initialize the graphic objects used by the application.
         */
        void GL_initialize() override {
            SimpleApplication::GL_initialize();

            // Create the texture and initialize its texturing modes
            glGenTextures(1, &texture_);
            glActiveTexture(GL_TEXTURE0 + GLUP_TEXTURE_2D_UNIT);
            glBindTexture(GL_TEXTURE_2D, texture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2Dxpm(uv);
            start_animation();
        }

        /**
         * \brief Draws the everting sphere.
         */
        void draw_scene() override {

            if(animate()) {
                time_ = float(
                    sin(double(anim_speed_) * GEO::Stopwatch::now())
                );
                time_ = 0.5f * (time_ + 1.0f);
            }

            if(transparent_) {
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                sphere_.set_alpha(alpha_);
            } else {
                glDisable(GL_BLEND);
            }

            if(mesh_) {
                glupEnable(GLUP_DRAW_MESH);
            } else {
                glupDisable(GLUP_DRAW_MESH);
            }

            if(smooth_) {
                glupEnable(GLUP_VERTEX_NORMALS);
            } else {
                glupDisable(GLUP_VERTEX_NORMALS);
            }

            if(textured_) {
                glupEnable(GLUP_TEXTURING);
                glActiveTexture(GL_TEXTURE0 + GLUP_TEXTURE_2D_UNIT);
                glBindTexture(GL_TEXTURE_2D, texture_);
                glupTextureType(GLUP_TEXTURE_2D);
                glupTextureMode(GLUP_TEXTURE_REPLACE);
            } else {
                glupDisable(GLUP_TEXTURING);
            }

            glupSetCellsShrink(shrink_);
            glupSetPointSize(point_size_);
            sphere_.set_time(double(time_));
            sphere_.set_rendering_style(style_);
            sphere_.set_nb_hemispheres_to_display(half_sphere_ ? 1 : 2);
            sphere_.set_show_half_strips(half_strips_);
            sphere_.set_lat_resolution(res_latitude_);
            sphere_.set_lon_resolution(res_longitude_);
            sphere_.set_nb_strips(nb_strips_);
            sphere_.set_nb_strips_to_display(
                int(float(nb_strips_) * proportion_strips_to_display_)
            );
            sphere_.set_bend_cylinder(bend_cylinder_);
            sphere_.draw();
        }

        void draw_about() override {
            ImGui::Separator();
            if(ImGui::BeginMenu("About...")) {
                ImGui::Text(
                    "     Animated Sphere Eversion\n"
                );
                ImGui::Separator();
                ImGui::Text(
                    "  Based on the original program by\n"
                    "      Nathaniel Thurston and\n"
                    "        Michael McGuffin\n"
                    "\n"
                );
                ImGui::Text(
                    "www.dgp.toronto.edu/~mjmcguff/eversion"
                );
                ImGui::Separator();
                ImGui::Text("\n");
                float sz = float(280.0 * std::min(scaling(), 2.0));
                ImGui::Image(
                    static_cast<ImTextureID>(geogram_logo_texture_),
                    ImVec2(sz, sz)
                );
                ImGui::Text("\n");
                ImGui::Text(
                    "\n"
                    "   GEOGRAM/GLUP Project homepage:\n"
                    "https://github.com/BrunoLevy/geogram\n"
                    "\n"
                    "      The ALICE project, Inria\n"
                );
                ImGui::EndMenu();
            }
        }

    private:
        EvertableSphere sphere_;
        float time_;
        float anim_speed_;
        bool bend_cylinder_;
        EvertableSphere::RenderingStyle style_;
        float point_size_;
        bool mesh_;
        float shrink_;
        int res_longitude_;
        int res_latitude_;
        int nb_strips_;
        bool half_sphere_;
        bool half_strips_;
        float proportion_strips_to_display_;
        bool transparent_;
        float alpha_;
        bool textured_;
        GLuint texture_;
        bool smooth_;
    };

}

int main(int argc, char** argv) {
    DemoEvertApplication app;
    app.start(argc, argv);
    return 0;
}
