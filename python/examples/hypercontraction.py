import exatn, numpy as np

def test_exatn():

    a1 = np.random.randn(2,3,4)
    print('A1 shape: ',a1.shape)

    b1 = np.random.randn(2,3,4,5)
    print('B1 shape: ',b1.shape)

    exatn.createTensor('C1', [3, 4, 5], 0.0)
    exatn.createTensor('A1', a1.copy(order='F'))
    exatn.createTensor('B1', b1.copy(order='F'))

    exatn.contractTensors('C1(b,c,d)=A1(a,b,c)*B1(a,b,c,d)',1.0)

    c1 = exatn.getLocalTensor('C1')
    print('C1 shape: ',c1.shape)
    print('ExaTN result c1 = a1 * b1:\n', c1)

    d1 = np.einsum('abc, abcd->bcd', a1, b1)
    print('D1 shape: ',d1.shape)
    print('NumPy result c1 = a1 * b1:\n', d1)

    exatn.destroyTensor('B1')
    exatn.destroyTensor('A1')
    exatn.destroyTensor('C1')

test_exatn()
