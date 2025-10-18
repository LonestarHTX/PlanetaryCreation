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


#include <geogram/basic/common.h>
#include <geogram/basic/logger.h>
#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_io.h>

// Tests support of large geofiles.

int main(int argc, char** argv) {
    using namespace GEO;

    GEO::initialize(GEO::GEOGRAM_INSTALL_ALL);
    CmdLine::import_arg_group("standard");
    if(!CmdLine::parse(argc, argv)) {
        return 1;
    }
    try {
	index_t nb_vertices = 0;
	{
	    CmdLine::ui_separator("Write large geofile");
	    index_t cell_nu = 744;
	    index_t cell_nv = 744;
	    index_t cell_nw = 376;
	    index_t node_nu = cell_nu + 1;
	    index_t node_nv = cell_nv + 1;
	    index_t node_nw = cell_nw + 1;

	    Mesh M;
	    Logger::out("geofile") << "Create vertices" << std::endl;

	    M.vertices.create_vertices(node_nu* node_nv* node_nw);
	    Logger::out("geofile") << "Init vertices" << std::endl;

	    FOR(k, node_nw) {
		FOR(j, node_nv) {
		    FOR (i, node_nu) {
			M.vertices.point(
			    node_nu* node_nv*k + j* node_nu + i
			) = vec3(
			    double(i) / double(node_nu),
			    double(j) / double(node_nv),
			    double(k) / double(node_nw)
			);
		    }
		}
	    }

	    Logger::out("geofile") << "save" << std::endl;
	    if(!mesh_save(M, "bigfile.geogram")) {
		Logger::err("geofile") << "Could not save file" << std::endl;
		return 1;
	    }
	    nb_vertices = M.vertices.nb();
	}

	{
	    CmdLine::ui_separator("Read large geofile");
	    Mesh M;
	    if(!mesh_load("bigfile.geogram", M)) {
		Logger::err("geofile") << "Could not load file" << std::endl;
		return 1;
	    }

	    if(M.vertices.nb() != nb_vertices) {
		Logger::err("geofile") << "Invalid number of vertices"
				       << std::endl;
		return 1;
	    }
	}
    } catch(const std::exception& e) {
        std::cerr << "Received an exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
