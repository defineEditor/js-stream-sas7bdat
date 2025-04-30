import DatasetSas7BDat from '../src/index';
import path from 'path';

test('Get unique values of variables', async () => {
    const filePath = path.join(__dirname, '/data/sample.sas7bdat');
    const data = new DatasetSas7BDat(filePath);
    const values = await data.getUniqueValues({columns: ['NAME', 'AGE', 'WEIGHT'], limit: 20});
    expect(values).toMatchSnapshot();
});

test('Get unique values of variables and do not sort the result', async () => {
    const filePath = path.join(__dirname, '/data/sample.sas7bdat');
    const data = new DatasetSas7BDat(filePath);
    const values = await data.getUniqueValues({columns: ['NAME', 'HEIGHT', 'WEIGHT'], limit: 20, bufferLength: 10, sort: false});
    expect(values).toMatchSnapshot();
});

test('Get unique values with counts', async () => {
    const filePath = path.join(__dirname, '/data/sample.sas7bdat');
    const data = new DatasetSas7BDat(filePath);
    const values = await data.getUniqueValues({columns: ['NAME', 'AGE', 'WEIGHT'],
        limit: 10, addCount: true, sort: false});
    expect(values).toMatchSnapshot();
});