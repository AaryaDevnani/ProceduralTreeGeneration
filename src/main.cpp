#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include "shader.h"
#include "cylinder.h"
#include "tree.h"
#include "leaf.h"
#include "camera.h"
#include "window.h"
#include "sphere.h"
#include "attraction_points.h"
#include "renderer.h"
#include "common_types.h"
#include "tree_nodes.h"
#include <vector>
#include <iostream> 
#include <memory> 
#include <variant>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define W_WIDTH 1200.0f
#define W_HEIGHT 900.0f

#define SHADER_PATH(name) SHADER_DIR name
#define BRANCH_LENGTH 0.2f
#define ROOT_BRANCH_COUNT (int)7
#define MAX_GROW (int)200



enum class Mode {
    LSystem,
    SpaceColonization
};

struct LSystemParameters {
    int depth;
    float scaleFactor;
	float branchRadius;
    int minLeafCount;
    int maxLeafCount;
    std::string axiom;
    std::unordered_map<char, std::string> rules;
};

struct SpaceColonizationParameters {
    float envelope_height;   // grow box height, determines the tree branch height
    float envelope_width;    // grow box width
    float envelope_length;   // grow box length
    float envelope_distance; // grow box distance from the bottom of the tree
    int envelope_density[3]; // number of attraction points per axis direction, determines how twisty and how long the tree branches are
};



Mode mode = Mode::LSystem;  // Default mode
bool showLeaves = true;

Camera* g_camera = nullptr;

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

void regenerateTree(Mode currentMode, Shader& shader,
    std::vector<glm::mat4>& branchTransforms,
    std::vector<glm::mat4>& leafTransforms,
    MeshRenderer::BufferObjects& cylinderBuffers,
    MeshRenderer::BufferObjects& leafBuffers,
    glm::mat4& model, std::variant<LSystemParameters, SpaceColonizationParameters> parameters ) {
    // Clear previous transformations

    branchTransforms.clear();
    leafTransforms.clear();

    // Delete old buffers
    MeshRenderer::deleteBuffers(cylinderBuffers);
    MeshRenderer::deleteBuffers(leafBuffers);

    // Create new buffers
    std::vector<float> cylinderVertices;
    std::vector<unsigned int> cylinderIndices;
    float branchLength = (currentMode == Mode::SpaceColonization) ? BRANCH_LENGTH + 0.04f : 1.0f;

	float branchRadius = 0.05f;
    if (mode == Mode::LSystem) {
	    branchRadius = 0.005f * std::get<LSystemParameters>(parameters).branchRadius;
    }

    Cylinder::create(cylinderVertices, cylinderIndices, branchRadius, branchLength, 8);
    cylinderBuffers = MeshRenderer::createBuffers(cylinderVertices, cylinderIndices);

    std::vector<float> leafVertices;
    std::vector<unsigned int> leafIndices;
    leaf::createLeaf(leafVertices, leafIndices);
    leafBuffers = MeshRenderer::createBuffers(leafVertices, leafIndices);

    // Generate the tree
    if (currentMode == Mode::LSystem) {
		LSystemParameters params = std::get<LSystemParameters>(parameters);
        std::string axiom = "X";
        std::unordered_map<char, std::string> rules = {
            {'X', "F[//+XXL][+++YXL][-&^FXL][&FXL][\\^FXL][--^FXL]"},
            {'F', "F[/+FL][-FL]"},
            {'Y', "F[\\+&FYL][/-+F^YL][/&F^Y*L][\\^FYL][F++++YL]"},
            {'L', "L[+L][-L][&L][^L]"}
        };
        Tree::createBranchesLSystem(model, branchTransforms, leafTransforms, params.axiom, rules, params.scaleFactor, 1.0f, params.depth, params.maxLeafCount, params.minLeafCount);
    }
    else if (mode == Mode::SpaceColonization) {
        SpaceColonizationParameters params = std::get<SpaceColonizationParameters>(parameters);
        // Create Atrtaction Points
        Envelope envelope;
        envelope.position = glm::vec3{ 0.1f, params.envelope_distance, 0.2f };

        envelope.positive_x = params.envelope_density[0];
        envelope.negative_x = params.envelope_density[0];
        envelope.positive_y = params.envelope_density[1];
        envelope.positive_z = params.envelope_density[2];
        envelope.negative_z = params.envelope_density[2];

        float x_interval = params.envelope_length / (2.0f * params.envelope_density[0]);
        float y_interval = params.envelope_height / params.envelope_density[1];
        float z_interval = params.envelope_width / (2.0f * params.envelope_density[2]);

        envelope.interval = glm::vec3(x_interval, y_interval, z_interval);

        AttractionPointManager attractionPoints(envelope);

        // Generate tree nodes on the root branch
        TreeNodeManager treeNodeManager(ROOT_BRANCH_COUNT);
        // First growth
        attractionPoints.UpdateLinks(treeNodeManager, 0.5f, 0.2f);

        int itr = 0;
        bool grew = true;
        while (grew != false && itr < MAX_GROW) {
            grew = treeNodeManager.GrowNewNodes(BRANCH_LENGTH);
            attractionPoints.UpdateLinks(treeNodeManager, 0.5f, 0.2f);
            itr++;
            if (itr % 50 == 0) {
                printf("%dth growth done. ", itr);
            }
        }

        Tree::createBranchesSpaceColonization(treeNodeManager.tree_nodes, model, branchTransforms, leafTransforms, 0.1f, 4, ROOT_BRANCH_COUNT);
    }



    // Update shader settings for new mode
    shader.use();
    shader.setMat4("model", model);
}



int main() {
    // Create and initialize window
    Window window(W_WIDTH, W_HEIGHT, "3D Tree");
    if (!window.init()) {
        return -1;
    }

    // Set up callbacks
    glfwSetScrollCallback(window.getHandle(), scroll_callback);

    // Create shader
    Shader shader(SHADER_PATH("vertex_shader.glsl"),
                  SHADER_PATH("fragment_shader.glsl"));
    // Generate cylinder mesh
    std::vector<float> cylinderVertices;
    std::vector<unsigned int> cylinderIndices;
    auto cylinderBuffers = MeshRenderer::createBuffers(cylinderVertices, cylinderIndices);

    // Generate branch transforms
    std::vector<glm::mat4> branchTransforms;
    glm::vec3 treePosition(0.0f, 0.0f, 0.0f); // Example: moves tree to x=-2, z=1
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, treePosition);
    

	// Generate leaf transforms
	std::vector<float> leafVertices;
	std::vector<unsigned int> leafIndices;
	leaf::createLeaf(leafVertices, leafIndices);
	auto leafBuffers = MeshRenderer::createBuffers(leafVertices, leafIndices);
	glm::mat4 leafModel = glm::mat4(1.0f);
	std::vector<glm::mat4> leafTransforms;

    // Default parameters

    LSystemParameters  DEFAULT_L_SYS_PARAMS = {
            3, 0.75f, 15.0, 10, 20, "X",
            {
                {'X', "F[//+XXL][+++YXL][-&^FXL][&FXL][\\^FXL][--^FXL][^&X]"},
                {'F', "F[/+FL][-FL]"},
                {'Y', "F[\\+&FYL][/-+F^YL][/&F^Y*L][\\^FYL][F++++YL]"},
                {'L', "L[+L][-L][&L][^L]"}
            }
    };

    LSystemParameters L_SYS_PRESET_PLANT = {
        2, 0.5, 5.0,5,15,"X",
        {
            {'X', "F[//+XXL][+++YXL][-&^FXL]"},
            {'F', "F[/+FL][-FL]"},
            {'Y', "F[\\+&FYL][/-+F^YL]"},
            {'L', "L[+L][-L][&L][^L]"}
        }
    };


    SpaceColonizationParameters DEFAULT_SPACE_COLONIZATION_PARAMS = {
            1.0f, 2.0f, 2.0f, 1.0f, {3, 3, 3}
    };

	glm::vec3 DEFAULT_LEAF_COLOR = glm::vec3(0.0f, 1.0f, 0.0f);

    static LSystemParameters lParams = DEFAULT_L_SYS_PARAMS;
    static SpaceColonizationParameters scParams = DEFAULT_SPACE_COLONIZATION_PARAMS;
	static glm::vec3 leafColor = DEFAULT_LEAF_COLOR;
	// only for the first generation
    std::variant<LSystemParameters, SpaceColonizationParameters> parameters;
	if (mode == Mode::LSystem) {
		parameters = DEFAULT_L_SYS_PARAMS;
	}
	else if (mode == Mode::SpaceColonization) {
		parameters = DEFAULT_SPACE_COLONIZATION_PARAMS;
	}
	regenerateTree(mode, shader, branchTransforms, leafTransforms, cylinderBuffers, leafBuffers, model, parameters);
    

    // UI init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io; // Suppress unused variable warning

    // Setup ImGui style
    ImGui::StyleColorsDark();

    // Initialize ImGui backends
    ImGui_ImplGlfw_InitForOpenGL(window.getHandle(), true);
    ImGui_ImplOpenGL3_Init("#version 130"); // Replace with your GLSL version

    // Light settings
    std::vector<glm::vec3> lightPositions = {
        glm::vec3(2.0f, 5.0f, 2.0f),
        glm::vec3(-2.0f, 3.0f, -2.0f)
    };
    std::vector<glm::vec3> lightColors = {
        glm::vec3(1.0f, 1.0f, 1.0f),
        glm::vec3(1.0f, 1.0f, 1.0f)
    };
    glm::vec3 treeColor(0.45f, 0.32f, 0.12f);
    glm::vec3 pointColor(1.0f, 0.0f, 0.0f);
    glm::vec3 nodeColor(0.0f, 1.0f, 0.0f);

    glm::vec3 cameraPos = treePosition + glm::vec3{0, 1, 0};
    // Create camera and set global pointer
    auto camera = std::make_unique<Camera>(W_WIDTH / W_HEIGHT, cameraPos);
    g_camera = camera.get();
    glViewport(0, 0, W_WIDTH, W_HEIGHT);

    // For calculating delta time
    float lastFrame = 0.0f;

    // Render loop
    while (!window.shouldClose()) {
        float currentFrame = glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glClearColor(0.8f, 0.9f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();

        // Update camera
        camera->processKeyboard(window.getHandle(), deltaTime);
        camera->update(deltaTime);

        // Get updated matrices
        glm::mat4 view = camera->getViewMatrix();
        glm::mat4 projection = camera->getProjectionMatrix();

        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        for (int i = 0; i < lightPositions.size(); i++) {
            shader.setVec3("lights[" + std::to_string(i) + "].position", lightPositions[i]);
            shader.setVec3("lights[" + std::to_string(i) + "].color", lightColors[i]);
        }
        shader.setInt("numLights", lightPositions.size());
        shader.setVec3("objectColor", treeColor);

         //Draw tree branches
        glBindVertexArray(cylinderBuffers.VAO);
        shader.setVec3("objectColor", treeColor);
        for (const auto& transform : branchTransforms) {
            shader.setMat4("model", transform);
            glDrawElements(GL_TRIANGLES, cylinderBuffers.indexCount, GL_UNSIGNED_INT, 0);
        }

        if (showLeaves) {
            //Draw Leaves
            glBindVertexArray(leafBuffers.VAO);
            shader.setVec3("objectColor", leafColor);
            for (const auto& transform : leafTransforms) {
                shader.setMat4("model", transform);
                glDrawElements(GL_TRIANGLES, leafBuffers.indexCount, GL_UNSIGNED_INT, 0);
            }
        }

        

        // close the window when esc is clicked
        if (glfwGetKey(window.getHandle(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window.getHandle(), true);
        }
      
        // Build ImGui UI
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        static bool toggle = false;
        ImGui::Begin("Toggle Mode");
        int temp = 0;
        if (ImGui::RadioButton("L-System Mode", mode == Mode::LSystem)) {
            mode = Mode::LSystem;
			parameters = DEFAULT_L_SYS_PARAMS;
            regenerateTree(mode, shader, branchTransforms, leafTransforms, cylinderBuffers, leafBuffers, model, parameters);
            
        }
        if (ImGui::RadioButton("Space Colonization Mode", mode == Mode::SpaceColonization)) {
            mode = Mode::SpaceColonization;
			parameters = DEFAULT_SPACE_COLONIZATION_PARAMS;
            regenerateTree(mode, shader, branchTransforms, leafTransforms, cylinderBuffers, leafBuffers, model, parameters);
        }
		ImGui::Checkbox("Show Leaves", &showLeaves);
        ImGui::End();

        ImGui::Begin("Parameters");

        // L-System Parameters
        if (mode == Mode::LSystem) {
            ImGui::InputInt("Depth", &lParams.depth);
            ImGui::InputFloat("Scale Factor", &lParams.scaleFactor);
			ImGui::InputFloat("Branch Radius", &lParams.branchRadius);
			ImGui::InputInt("Min Leaf Count", &lParams.minLeafCount);
			ImGui::InputInt("Max Leaf Count", &lParams.maxLeafCount);
            parameters = lParams;
        }

        // Space Colonization Parameters
        else if (mode == Mode::SpaceColonization) {
            ImGui::InputFloat("Envelope Height", &scParams.envelope_height);
            ImGui::InputFloat("Envelope Width", &scParams.envelope_width);
            ImGui::InputFloat("Envelope Length", &scParams.envelope_length);
            ImGui::InputFloat("Envelope Distance", &scParams.envelope_distance);
            for (int i = 0; i < 3; i++) {
                ImGui::InputInt(("Density Axis " + std::to_string(i + 1)).c_str(), &scParams.envelope_density[i]);
            }
            parameters = scParams;
        }

        if (showLeaves) {
		ImGui::ColorEdit3("Leaf Color", &leafColor[0]);
        }

       

        if (mode == Mode::LSystem) {
            ImGui::Text("Presets");
            ImGui::Separator(); // Draws a horizontal line
            if (ImGui::Button("Small Plant")) {
                lParams = L_SYS_PRESET_PLANT;
                leafColor = glm::vec3(0.0f, 1.0f, 0.0f);
                regenerateTree(mode, shader, branchTransforms, leafTransforms, cylinderBuffers, leafBuffers, model, lParams);
            }
            else if(ImGui::Button("Dense Tree")) {
				leafColor = glm::vec3(0.0f, 1.0f, 0.0f);
                lParams = DEFAULT_L_SYS_PARAMS;
				lParams.depth = 4;
				regenerateTree(mode, shader, branchTransforms, leafTransforms, cylinderBuffers, leafBuffers, model, lParams);
            }
            else if (ImGui::Button("Autumn Tree")) {
                lParams = L_SYS_PRESET_PLANT;
                lParams.scaleFactor = 0.75;
				lParams.branchRadius = 15.0;
                lParams.depth = 3;
                lParams.minLeafCount = 5;
				lParams.maxLeafCount = 7;
				leafColor = glm::vec3(1.0f, 0.5f, 0.0f);
				regenerateTree(mode, shader, branchTransforms, leafTransforms, cylinderBuffers, leafBuffers, model, lParams);

            }

        }


		ImGui::Separator();
        if (ImGui::Button("Regenerate")) {
            regenerateTree(mode, shader, branchTransforms, leafTransforms, cylinderBuffers, leafBuffers, model, parameters);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Default Params")) {
			if (mode == Mode::LSystem) {
				lParams = DEFAULT_L_SYS_PARAMS;
                leafColor = glm::vec3(0.0f, 1.0f, 0.0f);
                regenerateTree(mode, shader, branchTransforms, leafTransforms, cylinderBuffers, leafBuffers, model, lParams);
			}
			else if (mode == Mode::SpaceColonization) {
				scParams = DEFAULT_SPACE_COLONIZATION_PARAMS;
                leafColor = glm::vec3(0.0f, 1.0f, 0.0f);

                regenerateTree(mode, shader, branchTransforms, leafTransforms, cylinderBuffers, leafBuffers, model, scParams);
			}
			
		}

        
        ImGui::End();

        // Render ImGui
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window.getHandle(), &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());


        window.swapBuffers();
        window.pollEvents();
    }

    // Cleanup
    MeshRenderer::deleteBuffers(cylinderBuffers);

    // Camera will be automatically cleaned up when unique_ptr goes out of scope
    g_camera = nullptr;

    return 0;
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (g_camera) {
        g_camera->processMouseScroll(static_cast<float>(yoffset));
    }
}