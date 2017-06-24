#include "avro/Compiler.hh"
#include "avro/DataFile.hh"
#include "avro/Decoder.hh"
#include "nnweights.hxx"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <tuple>
#include <string>
#include <vector>

int main(int argc, char **argv) {
	std::ofstream coconut("coconut-server.cpp");

	coconut << R"(
#include "blaze/Math.h"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace blaze;

static constexpr unsigned long COLUMN_PADDING = 4;
static constexpr unsigned long MAX_SENTENCE_LENGTH = 60;
static constexpr unsigned long EMBED_DIMENSION = 50;

auto loadWord2Vec(const char *fname) {
  using ValueType = StaticMatrix<float, EMBED_DIMENSION, 1>;
  std::ifstream w2v(fname, std::ios::binary);
  std::unordered_map<std::string, ValueType> w2vmap;
  long long vocab, layer;

  w2v >> vocab >> layer;
  if (layer > EMBED_DIMENSION) {
    std::cerr << "Embedding size (" << layer << ") is larger than allowed (" << EMBED_DIMENSION << ")\n";
    std::terminate();
  }
  std::cerr << "Loading " << EMBED_DIMENSION << "-dimensional embeddings for " << vocab << " terms: ";

  auto start = std::chrono::steady_clock::now();
  for (long long i = 0; i < vocab; i++) {
    std::string word;
    std::vector<float> vec;
    vec.reserve(layer);

    w2v >> word;
    w2v.get(); // skip over space after word
    for (int j = 0; j < layer; j++) {
      float x;
      w2v.read((char *)&x, sizeof(x));
      vec.push_back(x);
    }
    w2vmap[word] = ValueType(EMBED_DIMENSION, 1, vec.data());
  }
  auto end = std::chrono::steady_clock::now();

  std::chrono::duration<double> time = end - start;
  std::cerr << time.count() << "s\n";
  return w2vmap;
}

int main(int argc, char **argv) {
)";

	std::vector<std::string> names{"question_convolution_filters", "question_convolution_biases",
	                               "answer_convolution_filters",   "answer_convolution_biases",
	                               "hidden_layer_weights",         "hidden_layer_biases",
	                               "softmax_layer_weights",        "softmax_layer_biases"};

	avro::DataFileReader<coconut::cnnweights> dfr(argv[1]);
	coconut::cnnweights weight;
	int i = 0;
	while (dfr.read(weight)) {
		const auto name = names[i++];
		double sz;

		switch (weight.dimension.size()) {
		case 3:
			std::cerr << name << " has size " << weight.dimension[0] << std::endl;
			coconut << "std::array<StaticMatrix<float, " << weight.dimension[1] << ", "
			        << weight.dimension[2] << ">, " << weight.dimension[0] << "> " << name << "{{\n";
			sz = weight.dimension[1] * weight.dimension[2];
			for (int n = 0; n < weight.dimension[0]; n++) {
              if (n != 0) {
                coconut << ",";
              }
				coconut << "StaticMatrix<float, " << weight.dimension[1] << ", "
				        << weight.dimension[2] << ">{";
				auto start = n * sz;
				auto end = (n + 1) * sz;
				coconut << "{";
				for (int w = start; w < end; w++) {
					if (w != start && w % weight.dimension[2] == 0)
						coconut << "}, {";
					coconut << weight.weights[w] << ", ";
				}
				coconut << "}}\n";
			}
			coconut << "}};\n";
			break;
		case 2:
			coconut << "StaticMatrix<float, " << weight.dimension[0] << ", " << weight.dimension[1]
			        << "> " << name << "{";
			sz = weight.dimension[1];
			for (int x = 0; x < weight.dimension[0]; x++) {
				auto start = x * sz;
				auto end = (x + 1) * sz;
                if (x != 0) { coconut << ","; }
				coconut << "{";
				for (int w = start; w < end; w++) {
                  if (w != start) { coconut << ","; }
					coconut << weight.weights[w];
                }
				coconut << "}";
			}
			coconut << "};\n";
			break;
		case 1:
			coconut << "StaticVector<float, " << weight.dimension[0] << ", rowVector> " << name
			        << "{";
			for (int i = 0; i < weight.weights.size(); i++) {
              if (i != 0) { coconut << ","; }
				coconut << weight.weights[i];
            }
			coconut << "};\n";
			break;
		}
	}

	std::random_device r;
	std::mt19937 gen(r());
	gen.seed(1234);
	std::uniform_real_distribution<float> dist(-.25, .25);
	coconut << "StaticMatrix<float, EMBED_DIMENSION, 1> unknown_word{";
	for (int i = 0; i < 50; i++) {
      if (i != 0) { coconut << ","; }
		coconut << "{" << dist(gen) << "}";
    }
	coconut << "};\n";

	for (auto part : {"question", "answer"}) {
		/* Get vectors for the results from the convolutions */
		coconut << "StaticVector<float, 100, rowVector> " << part << "_conv_map;\n";
		/* Create a matrix that's big enough for the query and padding */
		coconut << "HybridMatrix<float, EMBED_DIMENSION, MAX_SENTENCE_LENGTH + COLUMN_PADDING * 2> "
		        << part << "_input{EMBED_DIMENSION, MAX_SENTENCE_LENGTH + COLUMN_PADDING * 2};\n";
	}
	/* To store the convolution result */
	coconut << "HybridVector<float, MAX_SENTENCE_LENGTH, rowVector> "
	           "conv_result{MAX_SENTENCE_LENGTH};\n";

	coconut << R"(
    auto w2v_map = loadWord2Vec(argv[1]);

		transpose(hidden_layer_weights);
        /* Left pad */
        submatrix(question_input, 0, 0, EMBED_DIMENSION, COLUMN_PADDING) = 0;
        submatrix(answer_input, 0, 0, EMBED_DIMENSION, COLUMN_PADDING) = 0;

        std::fstream question_file{argv[2]};
        std::fstream answer_file{argv[3]};

        std::string question;
        std::string answer;

        auto start = std::chrono::steady_clock::now();
int pairs = 0;

  while (true) {
      getline(question_file, question);
      getline(answer_file, answer);

if (answer == "" || question == "") { break; }
pairs++;

)";

	for (auto part : {"question", "answer"}) {
		/* Load the query terms into a vector */
		coconut << "std::stringstream " << part << "_ss{" << part << "};\n";
		coconut << "std::vector<std::string> " << part << "_words{std::istream_iterator<std::string>{" << part << "_ss}, std::istream_iterator<std::string>{}};\n";
	}

	/* Prepare the input matrices for forwarding */
	for (auto part : {"question", "answer"}) {
		coconut << "\n";
		coconut << "" << part << "_input.resize(EMBED_DIMENSION, MAX_SENTENCE_LENGTH + COLUMN_PADDING * 2);\n";
		/* Set the relevant columns in the matrix to be the word2vec values, or if we can't find it,
		 * a random vector */
		coconut << "for (int i = 0; i < " << part << "_words.size(); i++) {\n";
		coconut << "  auto w2v_p = w2v_map.find(" << part << "_words[i]);\n";
		coconut << "  auto w2v = w2v_p == w2v_map.end() ? unknown_word : w2v_p->second;\n";
		coconut << "  submatrix(" << part << "_input, 0, i + COLUMN_PADDING, EMBED_DIMENSION, 1) = w2v;\n";
		coconut << "}\n";
		/* Right pad */
		coconut << "submatrix(" << part << "_input, 0, " << part << "_words.size() + COLUMN_PADDING, EMBED_DIMENSION, COLUMN_PADDING) = 0;\n";
		/* Reshape it to match the number of terms given */
		coconut << "" << part << "_input.resize(EMBED_DIMENSION, " << part << "_words.size() + 2 * COLUMN_PADDING);\n";
	}

	/* Start the forwarding */
	for (auto part : {"question", "answer"}) {
		/* Perform the convolutions */
		coconut << "conv_result.resize(" << part << "_words.size());\n";
		coconut << "for (int i = 0; i < " << part << "_convolution_filters.size(); i++) {\n";
		coconut << "  for (int k = 0; k < " << part << "_words.size(); k++) {\n";
		coconut << "    auto sub = submatrix(" << part << "_input, 0, k + COLUMN_PADDING, " << part << "_convolution_filters[i].rows(), " << part << "_convolution_filters[i].columns());\n";
		coconut << "    auto cc = sub % " << part << "_convolution_filters[i];\n";
		coconut << "    float sum = 0.0;\n";
		coconut << "    for (int j = 0; j < cc.rows(); j++) {\n";
		coconut << "      sum += std::accumulate(cc.begin(j), cc.end(j), 0.0);\n";
		coconut << "    }\n";
		coconut << "  conv_result[k] = sum;\n";
		coconut << "  }\n";
		coconut << "  " << part << "_conv_map[i] = max(conv_result);\n";
		coconut << "}\n";
		coconut << "" << part << "_conv_map = tanh(" << part << "_conv_map + " << part << "_convolution_biases);\n";
	}
	coconut << R"(
        StaticVector<float, 204, rowVector> joinLayer{0};
        subvector(joinLayer, 0, question_conv_map.size()) = question_conv_map;
        subvector(joinLayer, question_conv_map.size(), answer_conv_map.size()) = answer_conv_map;

        auto HiddenLayer = tanh((joinLayer * hidden_layer_weights) + hidden_layer_biases) * 2;
        auto FinalLayer = (HiddenLayer * trans(softmax_layer_weights)) + softmax_layer_biases;

        StaticVector<float, 2, rowVector> fmax(max(FinalLayer));
        auto submax = FinalLayer - fmax;
        auto expsubmax = exp(submax);
        auto sumexpsubmax = expsubmax[0] + expsubmax[1];

        /* std::cout << submax[1] - log(sumexpsubmax) << " in " << time.count() << "ms\n"; */
	}
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> time = end - start;

std::cout << pairs << " pairs in " << time.count() << "ms or " << (1000. * (pairs / time.count())) << " qps\n";
}
)";

	return EXIT_SUCCESS;
}
