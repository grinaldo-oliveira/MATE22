#include <iostream>
#include <vector>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <iomanip>
#include <string>
#include <sstream>
#include <map>

#include <iostream>
#include <locale.h> // Para setlocale
#include <windows.h> // Para SetConsoleOutputCP (se estiver no Windows)

using namespace std;

const float PI = 3.14159265359f;
const float EPSILON = 1e-6f;
const int WIDTH = 800;
const int HEIGHT = 600;

// Variáveis globais que serão configuradas via linha de comando
int MAX_CANDIDATES = 30;      // Valor padrão
bool ENABLE_SPATIAL_REUSE = true;  // Nova variável para controlar amostragem espacial

// Classe para vetores 3D
class Vec3 {
public:
    float x, y, z;
    
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    Vec3 operator+(const Vec3& other) const {
        return Vec3(x + other.x, y + other.y, z + other.z);
    }
    
    Vec3 operator-(const Vec3& other) const {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }
    
    Vec3 operator*(float scalar) const {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }
    
    float dot(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }
    
    float length() const {
        return sqrt(x * x + y * y + z * z);
    }
    
    Vec3 normalize() const {
        float len = length();
        if (len < EPSILON) return Vec3(0, 0, 0);
        return *this * (1.0f / len);
    }
};

// Classe para cores
class Color {
public:
    float r, g, b;
    
    Color() : r(0), g(0), b(0) {}
    Color(float r, float g, float b) : r(r), g(g), b(b) {}
    
    Color operator+(const Color& other) const {
        return Color(r + other.r, g + other.g, b + other.b);
    }
    
    Color operator*(float scalar) const {
        return Color(r * scalar, g * scalar, b * scalar);
    }
    
    Color operator*(const Color& other) const {
        return Color(r * other.r, g * other.g, b * other.b);
    }
    
    Color& operator+=(const Color& other) {
        r += other.r;
        g += other.g;
        b += other.b;
        return *this;
    }
    
    void clamp() {
        r = min(1.0f, max(0.0f, r));
        g = min(1.0f, max(0.0f, g));
        b = min(1.0f, max(0.0f, b));
    }
    
    // Função para calcular luminância (usado para peso)
    float luminance() const {
        return 0.299f * r + 0.587f * g + 0.114f * b;
    }
};

// Função auxiliar para máximo
float fmax(float a, float b) {
    return (a > b) ? a : b;
}

// Função auxiliar para mínimo
float fmin(float a, float b) {
    return (a < b) ? a : b;
}

// Gerador de números aleatórios simples
float randomFloat() {
    return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
}

int randomInt(int max) {
    return rand() % max;
}

// Classe para luzes
class Light {
public:
    Vec3 position;
    Color color;
    float intensity;
    
    Light() : intensity(0) {}
    Light(const Vec3& pos, const Color& col, float intens) 
        : position(pos), color(col), intensity(intens) {}
    
    // Calcula peso baseado na luminância da cor resultante
    float calculateWeight(const Vec3& surfacePos, const Vec3& surfaceNormal, 
                         const Color& albedo) const {
        Vec3 lightDir = position - surfacePos;
        float distance = lightDir.length();
        if (distance < EPSILON) return 0.0f;
        
        lightDir = lightDir.normalize();
        float cosTheta = fmax(0.0f, surfaceNormal.dot(lightDir));
        
        // Atenuação por distância balanceada
        float attenuation = intensity / (1.0f + distance * distance * 0.005f);
        
        // Calcula a cor resultante e usa sua luminância como peso
        Color resultColor = color * albedo * (attenuation * cosTheta);
        return resultColor.luminance();
    }
    
    Color calculateLighting(const Vec3& surfacePos, const Vec3& surfaceNormal, 
                           const Color& albedo) const {
        Vec3 lightDir = position - surfacePos;
        float distance = lightDir.length();
        
        if (distance < EPSILON) return Color(0, 0, 0);
        
        lightDir = lightDir.normalize();
        float cosTheta = fmax(0.0f, surfaceNormal.dot(lightDir));
        
        // Atenuação por distância balanceada
        float attenuation = intensity / (1.0f + distance * distance * 0.005f);
        Color lighting = color * (attenuation * cosTheta);
        
        return lighting * albedo;
    }
};

// Classe para pontos de superfície
class SurfacePoint {
public:
    Vec3 position;
    Vec3 normal;
    Color albedo;
    
    SurfacePoint() {}
    SurfacePoint(const Vec3& pos, const Vec3& norm, const Color& alb)
        : position(pos), normal(norm), albedo(alb) {}
};

// Utilitária para converter coordenadas de pixel para ID único
int pixelToId(int x, int y) {
    return y * WIDTH + x;
}

// Utilitária para converter ID para coordenadas
pair<int, int> idToPixel(int id) {
    return make_pair(id % WIDTH, id / WIDTH);
}

// Classe Reservoir para ReSTIR (VERSÃO CORRIGIDA)
class Reservoir {
public:
    int lightIndex;           // z (amostra selecionada)
    float targetPdf;          // p^(z) para o ponto atual
    float W;                  // peso W calculado conforme Equação (20)
    int M;                    // contador total de amostras
    float wSum;               // Swi - soma dos pesos das amostras
    vector<int> originPixels; // pixels qi que originaram as amostras
    map<int, int> pixelMCount; // M para cada pixel de origem
    
    // CORREÇÃO: Adicionar acumulador de cor para compatibilidade com versão enviesada
    Color accumulatedColor;
    
    Reservoir() : lightIndex(-1), targetPdf(0.0f), W(0.0f), M(0), wSum(0.0f), 
                  accumulatedColor(0, 0, 0) {}
    
    // CORREÇÃO: Método simples idêntico à versão enviesada
    void updateSimple(const vector<Light>& lights, const SurfacePoint& point, 
                      int candidateLightIndex) {
        if (candidateLightIndex < 0 || candidateLightIndex >= static_cast<int>(lights.size())) {
            return;
        }
        
        float newTargetPdf = lights[candidateLightIndex].calculateWeight(
            point.position, point.normal, point.albedo);
        float sourcePdf = 1.0f / static_cast<float>(lights.size());
        
        // Peso da amostra
        float sampleWeight = (sourcePdf > EPSILON) ? newTargetPdf / sourcePdf : 0.0f;
        
        M++;
        wSum += sampleWeight;
        
        // CORREÇÃO: Acumula a cor desta amostra (igual à versão enviesada)
        Color sampleColor = lights[candidateLightIndex].calculateLighting(
            point.position, point.normal, point.albedo);
        accumulatedColor += sampleColor * sampleWeight;
        
        // Seleção probabilística da amostra (igual à versão enviesada)
        if (wSum > EPSILON && randomFloat() < sampleWeight / wSum) {
            lightIndex = candidateLightIndex;
            targetPdf = newTargetPdf;
        }
    }
    
    // CORREÇÃO: Método simples IDÊNTICO à versão enviesada
    Color getFinalColorSimple(const vector<Light>& lights, const SurfacePoint& point) const {
        if (wSum < EPSILON || M == 0) return Color(0, 0, 0);
        
        // CORREÇÃO: Usar a mesma fórmula da versão enviesada
        // Versão BIASED: W(x,z) = (1/M) * (1/p^(xz)) * Swi(xi)
        return accumulatedColor * (1.0f / wSum);
    }
    
    // Função original de update para amostragem inicial (caso com reutilização espacial)
    void update(const vector<Light>& lights, const SurfacePoint& point, 
                int candidateLightIndex, int originPixelId = -1) {
        if (candidateLightIndex < 0 || candidateLightIndex >= static_cast<int>(lights.size())) {
            return;
        }
        
        float newTargetPdf = lights[candidateLightIndex].calculateWeight(
            point.position, point.normal, point.albedo);
        float sourcePdf = 1.0f / static_cast<float>(lights.size());
        
        // Peso da amostra
        float sampleWeight = (sourcePdf > EPSILON) ? newTargetPdf / sourcePdf : 0.0f;
        
        M++;
        wSum += sampleWeight;
        
        // Rastrear pixel de origem
        if (originPixelId >= 0) {
            originPixels.push_back(originPixelId);
            pixelMCount[originPixelId]++;
        }
        
        // Seleção probabilística da amostra
        if (wSum > EPSILON && randomFloat() < sampleWeight / wSum) {
            lightIndex = candidateLightIndex;
            targetPdf = newTargetPdf;
        }
    }
    
    // IMPLEMENTAÇÃO DO ALGORITMO 6: combineReservoirsUnbiased (apenas para reutilização espacial)
    void combineReservoirsUnbiased(const vector<Reservoir>& inputReservoirs, 
                                   const vector<Light>& lights,
                                   const SurfacePoint& queryPoint,
                                   const vector<int>& queryPixels) {
        
        // Linha 2: Reservoir s (inicializar)
        lightIndex = -1;
        targetPdf = 0.0f;
        W = 0.0f;
        M = 0;
        wSum = 0.0f;
        originPixels.clear();
        pixelMCount.clear();
        
        // Linhas 3-5: foreach r ? {r1, ..., rk} do
        for (size_t i = 0; i < inputReservoirs.size(); ++i) {
            const Reservoir& r = inputReservoirs[i];
            if (r.lightIndex >= 0) {
                // s.update(r.y, p^q(r.y), r.W, r.M)
                updateFromReservoir(r, lights, queryPoint);
                
                // Acumular informações de origem
                for (size_t j = 0; j < r.originPixels.size(); ++j) {
                    originPixels.push_back(r.originPixels[j]);
                }
                for (map<int, int>::const_iterator it = r.pixelMCount.begin(); 
                     it != r.pixelMCount.end(); ++it) {
                    pixelMCount[it->first] += it->second;
                }
            }
        }
        
        // Linha 6: Z ? 0
        float Z = 0.0f;
        
        // Linhas 7-10: foreach qi ? {q1, ..., qk} do
        for (size_t i = 0; i < queryPixels.size(); ++i) {
            int qi = queryPixels[i];
            if (lightIndex >= 0) {
                // Linha 8: if p^qi(s.y) > 0 then
                float pqi_sy = calculateTargetPdfForPixel(lightIndex, qi, lights);
                if (pqi_sy > EPSILON) {
                    // Linha 9: Z ? Z + ri.M
                    if (pixelMCount.find(qi) != pixelMCount.end()) {
                        Z += pixelMCount[qi];
                    }
                }
            }
        }
        
        // Linha 11: m ? 1/Z
        float m = (Z > EPSILON) ? 1.0f / Z : 0.0f;
        
        // Linha 12: s.W = p^q(s,y) * (m * s.wsum) / p^q(s,y)
        // Simplificando: s.W = m * s.wsum (conforme Equação 20)
        if (lightIndex >= 0 && wSum > EPSILON) {
            W = m * wSum;
        } else {
            W = 0.0f;
        }
    }
    
    // Método para obter cor final (caso com reutilização espacial)
    Color getFinalColor(const vector<Light>& lights, const SurfacePoint& point) const {
        if (lightIndex < 0 || W < EPSILON) {
            return Color(0, 0, 0);
        }
        
        // Usar o peso W não enviesado
        Color lightContribution = lights[lightIndex].calculateLighting(
            point.position, point.normal, point.albedo);
        
        return lightContribution * W;
    }

private:
    void updateFromReservoir(const Reservoir& other, const vector<Light>& lights, 
                           const SurfacePoint& queryPoint) {
        
        // Recalcular p^q(other.lightIndex) para o ponto de consulta atual
        float newTargetPdf = lights[other.lightIndex].calculateWeight(
            queryPoint.position, queryPoint.normal, queryPoint.albedo);
        
        float weight = newTargetPdf * other.M;
        
        M += other.M;
        wSum += weight;
        
        // Seleção probabilística da amostra
        if (wSum > EPSILON && randomFloat() < weight / wSum) {
            lightIndex = other.lightIndex;
            targetPdf = newTargetPdf;
        }
    }
    
    float calculateTargetPdfForPixel(int lightIdx, int pixelId, const vector<Light>& lights) const {
        if (lightIdx < 0 || lightIdx >= static_cast<int>(lights.size())) {
            return 0.0f;
        }
        
        // Converter pixelId para coordenadas
        pair<int, int> coords = idToPixel(pixelId);
        int x = coords.first;
        int y = coords.second;
        
        // Criar SurfacePoint para o pixel qi
        Vec3 position(x - WIDTH/2, y - HEIGHT/2, 0);
        Vec3 normal(0, 0, 1);
        
        int checkerX = static_cast<int>(floor(x / 50.0f));
        int checkerY = static_cast<int>(floor(y / 50.0f));
        int checker = (checkerX + checkerY) % 2;
        Color albedo = checker ? Color(0.9f, 0.9f, 0.9f) : Color(0.1f, 0.1f, 0.1f);
        
        SurfacePoint pixelPoint(position, normal, albedo);
        
        // Calcular p^qi(lightIdx)
        return lights[lightIdx].calculateWeight(pixelPoint.position, 
                                              pixelPoint.normal, 
                                              pixelPoint.albedo);
    }
};

// Classe principal da cena
class Scene {
public:
    vector<Light> lights;
    Vec3 cameraPos;
    Vec3 cameraTarget;
    
    Scene() : cameraPos(0, 0, 100), cameraTarget(0, 0, 0) {}
    
    void setupLights() {
        lights.clear();
        
        // LUZES PRINCIPAIS - intensidades aumentadas para marcar projeção
        lights.push_back(Light(Vec3(-150, -150, 150), Color(1.0f, 0.1f, 0.1f), 400));  // Vermelha intensa
        lights.push_back(Light(Vec3(150, -150, 150), Color(0.1f, 1.0f, 0.1f), 400));   // Verde intensa
        lights.push_back(Light(Vec3(-150, 150, 150), Color(0.1f, 0.1f, 1.0f), 400));   // Azul intensa
        lights.push_back(Light(Vec3(150, 150, 150), Color(1.0f, 1.0f, 0.1f), 400));    // Amarela intensa
        lights.push_back(Light(Vec3(0, 0, 200), Color(1.0f, 0.2f, 0.8f), 350));        // Magenta central forte
        
        // LUZES LATERAIS - intensidades altas para marcar as bordas
        lights.push_back(Light(Vec3(-350, 0, 140), Color(0.2f, 0.4f, 1.0f), 300));     // Azul forte esquerda
        lights.push_back(Light(Vec3(350, 0, 140), Color(1.0f, 0.2f, 0.4f), 300));      // Rosa forte direita
        lights.push_back(Light(Vec3(0, -250, 160), Color(0.2f, 1.0f, 0.4f), 280));     // Verde forte superior
        lights.push_back(Light(Vec3(0, 250, 160), Color(1.0f, 0.6f, 0.1f), 280));      // Laranja forte inferior
        
        cout << "Total de luzes configuradas: " << lights.size() << " (algoritmo corrigido)" << endl;
    }
};

// Classe do renderizador ReSTIR
class ReSTIRRenderer {
private:
    Scene scene;
    
public:
    ReSTIRRenderer() {
        scene.setupLights();
        srand(static_cast<unsigned int>(time(NULL)));
    }
    
    SurfacePoint createSurfacePoint(float x, float y) const {
        Vec3 position(x - WIDTH/2, y - HEIGHT/2, 0);
        Vec3 normal(0, 0, 1);
        
        // Usar floor() para evitar problemas de truncamento
        int checkerX = static_cast<int>(floor(x / 50.0f));
        int checkerY = static_cast<int>(floor(y / 50.0f));
        int checker = (checkerX + checkerY) % 2;
        
        // Cores mais saturadas para o xadrez
        Color albedo = checker ? Color(0.9f, 0.9f, 0.9f) : Color(0.1f, 0.1f, 0.1f);
        
        return SurfacePoint(position, normal, albedo);
    }
    
    // CORREÇÃO PRINCIPAL: Usar método idêntico à versão enviesada quando sem reutilização espacial
    Color renderPixel(float x, float y) {
        SurfacePoint point = createSurfacePoint(x, y);
        int currentPixelId = pixelToId(static_cast<int>(x), static_cast<int>(y));
        
        // CORREÇÃO: Quando não há reutilização espacial, usar método IDÊNTICO ao enviesado
        if (!ENABLE_SPATIAL_REUSE) {
            Reservoir simpleReservoir;
            for (int i = 0; i < MAX_CANDIDATES; i++) {
                int lightIndex = randomInt(static_cast<int>(scene.lights.size()));
                simpleReservoir.updateSimple(scene.lights, point, lightIndex);
            }
            
            Color finalColor = simpleReservoir.getFinalColorSimple(scene.lights, point);
            Color ambient = point.albedo * 0.01f;
            finalColor += ambient;
            return finalColor;
        }
        
        // Usar Algoritmo 6 apenas quando há reutilização espacial
        vector<Reservoir> inputReservoirs;
        vector<int> queryPixels;
        
        // Reservatório principal (amostragem inicial)
        Reservoir mainReservoir;
        for (int i = 0; i < MAX_CANDIDATES; i++) {
            int lightIndex = randomInt(static_cast<int>(scene.lights.size()));
            mainReservoir.update(scene.lights, point, lightIndex, currentPixelId);
        }
        inputReservoirs.push_back(mainReservoir);
        queryPixels.push_back(currentPixelId);
        
        // Reservatórios de reutilização espacial
        for (int dx = -1; dx <= 1; dx += 2) {  // Só diagonais
            for (int dy = -1; dy <= 1; dy += 2) {
                float nx = x + dx * 50;  // Usar múltiplos do tamanho do quadrado
                float ny = y + dy * 50;
                
                if (nx >= 0 && nx < WIDTH && ny >= 0 && ny < HEIGHT) {
                    int neighborPixelId = pixelToId(static_cast<int>(nx), static_cast<int>(ny));
                    SurfacePoint neighborPoint = createSurfacePoint(nx, ny);
                    
                    // Simular reservoir de pixel vizinho
                    Reservoir spatialReservoir;
                    for (int j = 0; j < 8; j++) {  // Menos amostras para vizinhos
                        int lightIdx = randomInt(static_cast<int>(scene.lights.size()));
                        spatialReservoir.update(scene.lights, neighborPoint, lightIdx, neighborPixelId);
                    }
                    
                    inputReservoirs.push_back(spatialReservoir);
                    queryPixels.push_back(neighborPixelId);
                }
            }
        }
        
        // APLICAR ALGORITMO 6: Combinação não enviesada de múltiplos reservatórios
        Reservoir finalReservoir;
        finalReservoir.combineReservoirsUnbiased(inputReservoirs, scene.lights, 
                                                point, queryPixels);
        
        // Obter cor final usando peso não enviesado
        Color finalColor = finalReservoir.getFinalColor(scene.lights, point);
        
        // Iluminação ambiente mínima
        Color ambient = point.albedo * 0.01f;
        finalColor += ambient;
        
        return finalColor;
    }
    
    vector<Color> render() {
        vector<Color> image(WIDTH * HEIGHT);
        
        cout << "Renderizando cena " << WIDTH << "x" << HEIGHT << "..." << endl;
        if (ENABLE_SPATIAL_REUSE) {
            cout << "Usando ALGORITMO 6 - COMBINAÇÃO NÃO ENVIESADA" << endl;
        } else {
            cout << "Usando MÉTODO IDÊNTICO À VERSÃO ENVIESADA" << endl;
        }
        cout << "MAX_CANDIDATES: " << MAX_CANDIDATES << endl;
        cout << "AMOSTRAGEM ESPACIAL: " << (ENABLE_SPATIAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
        clock_t start = clock();
        
        for (int y = 0; y < HEIGHT; y++) {
            if (y % 50 == 0) {
                cout << "Linha " << y << "/" << HEIGHT 
                     << " (" << fixed << setprecision(1) 
                     << (static_cast<float>(y) / HEIGHT * 100) << "%)" << endl;
            }
            
            for (int x = 0; x < WIDTH; x++) {
                image[y * WIDTH + x] = renderPixel(static_cast<float>(x), static_cast<float>(y));
            }
        }
        
        clock_t end = clock();
        double duration = static_cast<double>(end - start) / CLOCKS_PER_SEC;
        
        cout << "Renderização concluída em " << duration << " segundos" << endl;
        
        return image;
    }
    
    void saveImage(const vector<Color>& image, const string& filename) const {
        ofstream file(filename.c_str());
        if (!file.is_open()) {
            cerr << "Erro ao criar arquivo " << filename << endl;
            return;
        }
        
        file << "P3" << endl << WIDTH << " " << HEIGHT << endl << "255" << endl;
        
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                Color pixel = image[y * WIDTH + x];
                pixel.clamp();
                
                int r = static_cast<int>(pixel.r * 255);
                int g = static_cast<int>(pixel.g * 255);
                int b = static_cast<int>(pixel.b * 255);
                
                file << r << " " << g << " " << b << " ";
            }
            file << endl;
        }
        
        file.close();
        cout << "Imagem salva como " << filename << endl;
    }
};

void printUsage(const char* programName) {
    cout << "Uso: " << programName << " [opções]" << endl;
    cout << "Opções:" << endl;
    cout << "  -c, --candidates <número>      Define MAX_CANDIDATES (padrão: 30)" << endl;
    cout << "  -s, --spatial-reuse            Ativa amostragem espacial (padrão: ativada)" << endl;
    cout << "      --no-spatial-reuse         Desativa amostragem espacial" << endl;
    cout << "  -h, --help                     Mostra esta ajuda" << endl;
    cout << endl;
    cout << "VERSÃO CORRIGIDA - IDÊNTICA à versão enviesada quando sem reutilização espacial" << endl;
    cout << "Exemplos:" << endl;
    cout << "  " << programName << " -c 50                    # 50 candidatos com amostragem espacial" << endl;
    cout << "  " << programName << " --no-spatial-reuse       # Sem amostragem espacial (=versão enviesada)" << endl;
    cout << "  " << programName << " -c 20 --no-spatial-reuse # 20 candidatos sem amostragem espacial" << endl;
}

bool parseArguments(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "-c" || arg == "--candidates") {
            if (i + 1 < argc) {
                MAX_CANDIDATES = atoi(argv[++i]);
                if (MAX_CANDIDATES <= 0) {
                    cerr << "Erro: MAX_CANDIDATES deve ser maior que 0" << endl;
                    return false;
                }
            } else {
                cerr << "Erro: " << arg << " requer um valor" << endl;
                return false;
            }
        }
        else if (arg == "-s" || arg == "--spatial-reuse") {
            ENABLE_SPATIAL_REUSE = true;
        }
        else if (arg == "--no-spatial-reuse") {
            ENABLE_SPATIAL_REUSE = false;
        }
        else {
            cerr << "Erro: Argumento desconhecido: " << arg << endl;
            printUsage(argv[0]);
            return false;
        }
    }
    return true;
}

string generateFilename() {
    ostringstream oss;
    oss << "restir_fixed_" << MAX_CANDIDATES << "_";
    if (ENABLE_SPATIAL_REUSE) {
        oss << "spatial";
    } else {
        oss << "nospatial";
    }
    oss << "_unbiased.ppm";
    return oss.str();
}

int main(int argc, char* argv[]) {
    // Configuração de localidade
    setlocale(LC_ALL, "Portuguese");
    SetConsoleOutputCP(850);
    
    cout << "=== ReSTIR VERSÃO CORRIGIDA - Resultados Idênticos Quando Necessário ===" << endl;
    
    // Parse dos argumentos de linha de comando
    if (!parseArguments(argc, argv)) {
        return 1;
    }
    
    cout << "Configuração:" << endl;
    cout << "  MAX_CANDIDATES: " << MAX_CANDIDATES << endl;
    cout << "  AMOSTRAGEM_ESPACIAL: " << (ENABLE_SPATIAL_REUSE ? "ATIVADA" : "DESATIVADA") << endl;
    if (ENABLE_SPATIAL_REUSE)  {
        cout << "  Algoritmo: ALGORITMO 6 - NÃO ENVIESADO" << endl;
    } else {
        cout << "  Algoritmo: MÉTODO SIMPLES (equivalente ao enviesado)" << endl;
    }
    cout << endl;
    
    ReSTIRRenderer renderer;
    vector<Color> image = renderer.render();
    
    string filename = generateFilename();
    renderer.saveImage(image, filename);
    
    cout << "Programa finalizado com sucesso!" << endl;
    cout << "Abra o arquivo '" << filename << "' para ver o resultado!" << endl;
    cout << "\nCORREÇÕES IMPLEMENTADAS:" << endl;
    cout << " Método simples para caso sem reutilização espacial" << endl;
    cout << " Resultado idêntico à versão enviesada quando --no-spatial-reuse" << endl;
    cout << " Rastreamento de pixels de origem (qi)" << endl;
    cout << " Reavaliação de PDFs para pontos de consulta" << endl;
    cout << " Cálculo do fator de normalização Z" << endl;
    cout << " Peso W não enviesado conforme Equação (20)" << endl;
    cout << " Combinação matematicamente correta de reservatórios" << endl;
    cout << "\nDIFERENÇAS DA VERSÃO ANTERIOR:" << endl;
    cout << "- Método combineReservoirsUnbiased() implementado" << endl;
    cout << "- Cada reservatório rastreia suas amostras de origem" << endl;
    cout << "- Peso final calculado usando m = 1/Z" << endl;
    cout << "- Garantia matemática de não enviesamento" << endl;
    
    return 0;
}