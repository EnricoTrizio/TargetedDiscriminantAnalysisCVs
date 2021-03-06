/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2011-2018 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "function/Function.h"
#include "function/ActionRegister.h"

#include <torch/torch.h>
#include <torch/script.h>

#include <cmath>

//DEBUG
#include <chrono>

using namespace std;

std::vector<float> tensor_to_vector(const torch::Tensor& x) {
    return std::vector<float>(x.data_ptr<float>(), x.data_ptr<float>() + x.numel());
}

namespace PLMD {
namespace function {

//+PLUMEDOC FUNCTION PYTORCH MODEL
/*
Load a model trained with Pytorch. The derivatives are set using native backpropagation in Pytorch.

\par Examples
Define a model that takes as inputs two distances d1 and d2 

\plumedfile
model: PYTORCH_MODEL MODEL=model.pt ARG=d1,d2
\endplumedfile

The N nodes of the neural network are saved as "model.node-0", "model.node-1", ..., "model.node-(N-1)".

*/
//+ENDPLUMEDOC


class PytorchModel2 :
  public Function
{
  unsigned _n_in;
  unsigned _n_out;
  torch::jit::script::Module _model;
public:
  explicit PytorchModel2(const ActionOptions&);
  void calculate();
  static void registerKeywords(Keywords& keys);
};


PLUMED_REGISTER_ACTION(PytorchModel2,"PYTORCH_MODEL2")

void PytorchModel2::registerKeywords(Keywords& keys) {
  Function::registerKeywords(keys);
  keys.use("ARG");
  keys.add("optional","MODEL","filename of the trained model"); 
  keys.addOutputComponent("node", "default", "NN outputs"); 
}

PytorchModel2::PytorchModel2(const ActionOptions&ao):
  Action(ao),
  Function(ao)
{ //print pytorch version

  //number of inputs of the model
  _n_in=getNumberOfArguments();

  //parse model name
  std::string fname="model.pt";
  parse("MODEL",fname); 
 
  //deserialize the model from file
  try {
    _model = torch::jit::load(fname);
  }
  catch (const c10::Error& e) {
    error("Cannot load Pytorch model. Check that the model is present and that the version of Pytorch is compatible with the Libtorch linked to PLUMED.");    
  }

  checkRead();

  //check the dimension of the output
  log.printf("Checking output dimension:\n");
  std::vector<float> input_test (_n_in);
  torch::Tensor single_input = torch::tensor(input_test).view({1,_n_in});  
  std::vector<torch::jit::IValue> inputs;
  inputs.push_back( single_input );
  torch::Tensor output = _model.forward( inputs ).toTensor(); 
  vector<float> cvs = tensor_to_vector (output);
  _n_out=cvs.size();

  //create components
  for(unsigned j=0; j<_n_out; j++){
    string name_comp = "node-"+std::to_string(j);
    addComponentWithDerivatives( name_comp );
    componentIsNotPeriodic( name_comp );
  }
 
  //print log
  //log.printf("Pytorch Model Loaded: %s \n",fname);
  log.printf("Number of input: %d \n",_n_in); 
  log.printf("Number of outputs: %d \n",_n_out); 

}

void PytorchModel2::calculate() {
  //DEBUG
  // preallocate time variables
  //auto start = std::chrono::system_clock::now();
  //auto end = std::chrono::system_clock::now();
  //auto elapsed = end - start;
  //DEBUG
  //start = std::chrono::system_clock::now(); 

  //retrieve arguments
  vector<float> current_S(_n_in);
  for(unsigned i=0; i<_n_in; i++)
    current_S[i]=getArgument(i);
  //convert to tensor
  torch::Tensor input_S = torch::tensor(current_S).view({1,_n_in});
  input_S.set_requires_grad(true);
  //convert to Ivalue
  std::vector<torch::jit::IValue> inputs;
  inputs.push_back( input_S );
  //calculate output
  torch::Tensor output = _model.forward( inputs ).toTensor();  
  //set CV values
  vector<float> cvs = tensor_to_vector (output);
  for(unsigned j=0; j<_n_out; j++){
    string name_comp = "node-"+std::to_string(j);
    getPntrToComponent(name_comp)->set(cvs[j]);
  }
  //DEBUG
  //end = std::chrono::system_clock::now();
  //elapsed = end - start;
  //std::cout << "(1) Forward: " << std::endl;
  //std::cout << "duration: " << elapsed.count() << std::endl;
  //start = std::chrono::system_clock::now();
  //derivatives
  for(unsigned j=0; j<_n_out; j++){
//    std::cout << "j: " << j  << std::endl;
    //output[0][j].backward();
//    std::cout << output.slice(1,j,j+1)  << std::endl;
    int batch_size = 1; 
    auto grad_output = torch::ones({1}).expand({batch_size, 1}); 
//    std::cout << "grad_output"  << std::endl;
    auto gradient = torch::autograd::grad({output.slice(/*dim=*/1, /*start=*/j, /*end=*/j+1)},
                                          {input_S},
                                          /*grad_outputs=*/{grad_output},
                                          /*retain_graph=*/true,
                                          /*create_graph=*/false);
//    std::cout << "gradient"  << std::endl;
//    std::cout << gradient << std::endl;
    auto grad = gradient[0].unsqueeze(/*dim=*/1);
//    std::cout << "grad"  << std::endl;
//    std::cout << grad << std::endl;
    //convert to vector
    vector<float> der = tensor_to_vector ( grad );
//    std::cout << "der"  << std::endl;
    //vector<float> der = tensor_to_vector (input_S.grad() );
//    std::cout << der  << std::endl;
    string name_comp = "node-"+std::to_string(j);
    //set derivatives of component j
    for(unsigned i=0; i<_n_in; i++)
      setDerivative( getPntrToComponent(name_comp) ,i, der[i] );
//    std::cout << "der assigned!!"  << std::endl;
    //reset gradients
    //input_S.grad().zero_();
    //for(unsigned i=0; i<_n_in; i++)
    //	input_S.grad()[0][i] = 0.;
 
  }
  //DEBUG
  //end = std::chrono::system_clock::now();
  //elapsed = end - start;
  //std::cout << "(2) Derivatives: " << std::endl;
  //std::cout << "duration: " << elapsed.count() << std::endl;
}
}
}
