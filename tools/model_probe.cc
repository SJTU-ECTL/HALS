
#include "design_error_model.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    DesignErrorModel model;
    if (!model.Load(argv[1])) return 3;
    std::ifstream input(argv[2]);
    std::vector<double> x(model.FeatureNum());
    std::cout << std::setprecision(17);
    while (input >> x[0]) {
        for (size_t i=1; i<x.size(); ++i) if (!(input >> x[i])) return 4;
        std::cout << "PRED " << model.Predict(x) << " " << model.InFeatureRange(x) << "\n";
    }
}
