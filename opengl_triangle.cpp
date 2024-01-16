#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}

int main() {

    //初始glfw
    glfwInit();
    //设置主版本号
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    //设置次版本号
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    //声明OpenGL使用核心模式
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);//MAC OS X系统加上该声明

    GLFWwindow *window;
    //创建窗口
    window = glfwCreateWindow(800, 600, "OpenGL Window", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create window!" << std::endl;
        glfwTerminate();
        return -1;
    }
    //将window的context设置为当前线程的context
    glfwMakeContextCurrent(window);

    //glad用于寻找OpenGL的函数，
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to init glad loader!" << std::endl;
        glfwTerminate();
        return -1;
    }

    //定义三角形三个顶点的坐标，第三个值为z轴，平面三角形z轴为0
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,
        0.5f, -0.5f, 0.0f,
        0.0f,  0.5f, 0.0f
    };

    //生成顶点缓冲对象，顶点缓冲对象会在GPU内存中储存大量的顶点数据
    unsigned int VBO;
    glGenBuffers(1, &VBO);
    //顶点缓冲对象的缓冲类型是GL_ARRAY_BUFFER，绑定顶点缓冲对象
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    //将三角形顶点的数据复制到顶点缓冲对象中
    /*
    glglBufferData的第四个参数有以下三种形式：
        GL_STATIC_DRAW ：数据不会或几乎不会改变。
        GL_DYNAMIC_DRAW：数据会被改变很多。
        GL_STREAM_DRAW ：数据每次绘制时都会改变。
    */
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    //顶点着色器源码
    const char *vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
    "}\0";
    //创建顶点着色器对象
    unsigned int vertexShader;
    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    //将顶点着色器源码附着到顶点着色器对象上
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    //编译顶点着色器
    glCompileShader(vertexShader);
    int success;
    char infoLog[512];
    //检测顶点着色器是狗编译成功
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "Failed to compile vertext shader: " << infoLog << std::endl;
        return -1;
    }

    //片段着色器源码
    const char *fragmentShaderSource = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);\n"
    "}\0";
    unsigned int fragmentShader;
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "Failed to compile fragment shader: " << infoLog << std::endl;
        return -1;
    }

    unsigned int shaderProgram;
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if(!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "Failed to link program: " << infoLog << std::endl;
        return -1;
    }
    glUseProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    //创建顶点数组对象
    unsigned int VAO;
    glGenVertexArrays(1, &VAO);

    // 1. 绑定VAO
    glBindVertexArray(VAO);
    // 2. 把顶点数组复制到缓冲中供OpenGL使用
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    // 3. 设置顶点属性指针
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 4. 绘制物体
    glUseProgram(shaderProgram);
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    //交换buffer
    glfwSwapBuffers(window);

    //指定用于渲染的视图大小
    glViewport(0, 0, 800, 600);
    //设置窗口大小变化时的回调函数
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    //轮询事件，当检测到退出事件时退出循环
    while (!glfwWindowShouldClose(window)) {

        processInput(window);

        //设置用于清屏的颜色
        //glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        //清屏
        //glClear(GL_COLOR_BUFFER_BIT);

        

        glfwPollEvents();
        //glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();

    return 0;
}
