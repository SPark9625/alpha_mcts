#ifndef network_hpp
#define network_hpp

#include <iostream>
#include <tuple>
#include <vector>

#include <torch/torch.h>

struct ConvBlockImpl : torch::nn::Module {
    ConvBlockImpl(int in, int out, /* channels out */ int K, int P)
      : conv(register_module("conv", torch::nn::Conv2d(torch::nn::Conv2dOptions(in, out, K).padding(P)))),
        batch_norm(register_module("bn", torch::nn::BatchNorm(torch::nn::BatchNormOptions(out).momentum(0.9)))) { /* pass */ }

    torch::Tensor forward(const torch::Tensor& x){
        return torch::relu(batch_norm(conv(x)));
    }

    torch::nn::Conv2d conv;
    torch::nn::BatchNorm batch_norm;
};
TORCH_MODULE(ConvBlock);


struct ResBlockImpl : torch::nn::Module {
    ResBlockImpl(int in, int out, int K, int P)
      : conv1(register_module("conv1", torch::nn::Conv2d(torch::nn::Conv2dOptions(in, out, K).padding(P)))),
        conv2(register_module("conv2", torch::nn::Conv2d(torch::nn::Conv2dOptions(out, out, K).padding(P)))),
        batch_norm1(register_module("bn1", torch::nn::BatchNorm(torch::nn::BatchNormOptions(out).momentum(0.9)))),
        batch_norm2(register_module("bn2", torch::nn::BatchNorm(torch::nn::BatchNormOptions(out).momentum(0.9)))) { /* pass */ }

    torch::Tensor forward(const torch::Tensor& input)
    {
        torch::Tensor x = torch::relu(batch_norm1(conv1(input)));
        x = batch_norm2(conv2(x));
        x = torch::relu(x + input);
        return x;
    }

    torch::nn::Conv2d conv1, conv2;
    torch::nn::BatchNorm batch_norm1, batch_norm2;
};
TORCH_MODULE(ResBlock);


struct PolicyHeadImpl : torch::nn::Module {
    PolicyHeadImpl(int C, int out, bool train)
      : conv(register_module("conv", torch::nn::Conv2d(C, out, 1))),
        training(train) { /* pass */ }
    
    torch::Tensor forward(const torch::Tensor& input)
    {
        torch::Tensor x = conv(input);
        auto shape = x.sizes();
        if (training) {
            x = x.flatten(1).log_softmax(1).view(shape);
        } else {
            x = x.flatten(1).softmax(1).view(shape);
        }
        return x;
    }
    torch::nn::Conv2d conv;
    bool training;
};
TORCH_MODULE(PolicyHead);


struct ValueHeadImpl : torch::nn::Module {
    ValueHeadImpl(int C, int size_2)
      : value_conv(register_module("conv", torch::nn::Conv2d(C, 1, 1))),
        value_bn(register_module("bn", torch::nn::BatchNorm(torch::nn::BatchNormOptions(1).momentum(0.9)))),
        value_fc1(register_module("fc1", torch::nn::Linear(size_2, 64))),
        value_fc2(register_module("fc2", torch::nn::Linear(64, 2))) { /* pass */ }
    
    torch::Tensor forward(const torch::Tensor& input)
    {
        torch::Tensor x = torch::relu(value_bn(value_conv(input)));  // size x size
        x = x.flatten(1);  // size^2
        x = torch::relu(value_fc1(x));
        x = value_fc2(x);
        x = torch::softmax(x, 1) * 2 - 1;
        return x;
    }

    torch::nn::Conv2d value_conv;
    torch::nn::BatchNorm value_bn;
    torch::nn::Linear value_fc1;
    torch::nn::Linear value_fc2;
};
TORCH_MODULE(ValueHead);

/*
 * Actual network used in AGZ.
 *
 * board_size: Size of the board (3 in case of TicTacToe)
 * n_res     : # of ResBlock
 * Cs        : # of channels for each ResBlock
 * in        : # of input channels
 * out       : # of output channels
 * K         : Kernel size
 * P         : Padding (only applied where K == 3)
 */
struct PVNetworkImpl : torch::nn::Module {
    PVNetworkImpl(int board_size, const std::vector<int>& Cs, int in, int out, bool training = false, int K = 3, int P = 1)
      : num_res(Cs.size() - 1),
        CBlock(register_module("CBlock", ConvBlock(in, Cs[0], K, P))),
        PHead(register_module("PHead", PolicyHead(Cs[num_res - 1], out, training))),
        VHead(register_module("VHead", ValueHead(Cs[num_res - 1], board_size*board_size)))
    {
        for (int i = 0; i < num_res; i++) {
            int C_in = Cs[i];
            int C_out = Cs[i + 1];
            RBlocks.emplace_back(register_module("RBlock_" + std::to_string(i), ResBlock(C_in, C_out, K, P)));
        }
    }

    std::tuple<torch::Tensor, torch::Tensor> forward(const torch::Tensor& input)
    {
        torch::Tensor x = CBlock(input);
        for (int i = 0; i < num_res; i++)
            x = RBlocks[i](x);
        return std::make_tuple(PHead(x), VHead(x));
    }

    int num_res;

    ConvBlock CBlock;
    std::vector<ResBlock> RBlocks;
    PolicyHead PHead;
    ValueHead VHead;
};
TORCH_MODULE(PVNetwork);

#endif // network_hpp