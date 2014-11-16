import std.stdio;
import glfw3;
import derelict.opengl3.gl3;

int width, height;

void main(string args[])
{
	writeln(glfwInit());

	int* a = null;
	GLFWwindow* window = null;
	glfwHideWindow(window);

/*	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	GLFWwindow* window = glfwCreateWindow(mode.width*2, mode.height*2, "Kakao", glfwGetPrimaryMonitor(), null);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(0VSYNC);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwGetFramebufferSize(window, &width, &height);*/
}
