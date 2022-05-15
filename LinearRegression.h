#pragma warning(push)
#pragma warning(disable : 4459)
#pragma warning(disable : 4458)
#pragma warning(disable : 4121)
#pragma warning(disable : 4127)
#pragma warning(disable : 4456)
#include <dlib/svm.h>
#pragma warning(pop)
#include <iostream>
#include <vector>

#include <Log.h>

typedef std::pair<std::vector<double>, double> DataPoint;
typedef std::vector<DataPoint> DataSet;

std::vector<double> runLeastSquares(DataSet dataSet)
{
    try
    {
        auto a = dlib::rls();
        for (auto const& dataPoint : dataSet) {
            dlib::matrix<double> data(dataPoint.first.size(), 1);
            for (int i = 0; i < int(dataPoint.first.size()); ++i) {
                data(i, 0) = dataPoint.first[i];
            }
            a.train(data, dataPoint.second);
        }
        auto const& weights = a.get_w();
        std::vector<double> returnVal(weights.begin(), weights.end());
        return returnVal;
    }
    catch (std::exception& e)
    {
        logger << e.what() << "\n";
        return std::vector<double>();
    }
}
